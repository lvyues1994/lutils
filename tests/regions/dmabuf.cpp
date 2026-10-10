#include "Fixture.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/image/LinuxImage.hpp>
#include <sys/ioctl.h>
#ifdef LUTILS_TEST_GBM
#include <gbm.h>
#endif
#ifdef LUTILS_TEST_REGION_DEVICE
#include <lutils/compute/Vulkan.hpp>
#include <lutils/image/DeviceRegion.hpp>
#endif

using namespace regions_test;
int main(int argc, char **argv) {
    std::string heapPath = "/dev/dma_heap/system";
    std::string renderPath = "/dev/dri/renderD128";
    bool vulkan = false, strict = false, validation = false;
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        if (option == "--vulkan")
            vulkan = true;
        else if (option == "--require-supported")
            strict = true;
        else if (option == "--validation")
            validation = true;
        else if (option == "--heap" && i + 1 < argc)
            heapPath = argv[++i];
        else if (option == "--render" && i + 1 < argc)
            renderPath = argv[++i];
        else {
            std::cerr << "unknown option: " << option << '\n';
            return 1;
        }
    }
    auto unavailable = [&](std::string const &reason) {
        std::cout << "dma-buf interop unavailable: " << reason << '\n';
        return strict ? 1 : 77;
    };
    co::FileDescriptor heap{open(heapPath.c_str(), O_RDWR | O_CLOEXEC)};
#ifdef LUTILS_TEST_GBM
    co::FileDescriptor render;
    std::unique_ptr<gbm_device, decltype(&gbm_device_destroy)> gbm{nullptr, gbm_device_destroy};
    if (heap.get() < 0) {
        render = co::FileDescriptor{open(renderPath.c_str(), O_RDWR | O_CLOEXEC)};
        if (render.get() >= 0)
            gbm.reset(gbm_create_device(render.get()));
        if (!gbm)
            return unavailable("neither dma-heap nor GBM allocator is accessible");
        std::cout << "allocator: GBM " << renderPath << '\n';
    } else
        std::cout << "allocator: dma-heap " << heapPath << '\n';
#else
    if (heap.get() < 0)
        return unavailable(heapPath + ": " + std::strerror(errno));
#endif
    std::unique_ptr<co::Device> device;
    auto executor = im::createCpuRegionExecutor();
#ifdef LUTILS_TEST_REGION_DEVICE
    auto stats = std::make_shared<co::VulkanStatistics>();
    auto report = std::make_shared<co::ValidationReport>();
    if (vulkan) {
        co::VulkanOptions options;
        options.requireHardware = true;
        options.enableValidation = validation;
        options.statistics = stats;
        options.validationReport = report;
        auto result = co::createVulkanDevice(options);
        if (!result)
            return unavailable(result.error().message);
        device = std::move(result).value();
        if (!device->info().capabilities.externalDmaBuf)
            return unavailable("Vulkan lacks dma-buf/sync-file capabilities");
        executor = take(im::createDeviceRegionExecutor(*device));
        std::cout << device->info().name << '\n';
    }
#else
    (void)validation;
    if (vulkan)
        return unavailable("build with kernelc and Vulkan to test GPU interop");
#endif
    for (auto const &spec : formats())
        for (bool split : {false, true}) {
            Storage src(spec, 12, 10, 1, 3), dst(spec, 16, 12, 3, 1);
            src.fill(spec);
            auto allocateOne = [&](Storage const &storage) -> lutils::Result<im::ImageResource> {
                co::FileDescriptor fd;
                if (heap.get() >= 0) {
                    dma_heap_allocation_data allocation{};
                    allocation.len = 4096;
                    allocation.fd_flags = O_RDWR | O_CLOEXEC;
                    if (ioctl(heap.get(), DMA_HEAP_IOCTL_ALLOC, &allocation) < 0)
                        return lutils::Error{lutils::ErrorCode::Unsupported, std::strerror(errno)};
                    fd = co::FileDescriptor{static_cast<int>(allocation.fd)};
                }
#ifdef LUTILS_TEST_GBM
                else {
                    std::uint64_t linear = 0;
                    std::unique_ptr<gbm_bo, decltype(&gbm_bo_destroy)> bo{
                        gbm_bo_create_with_modifiers(gbm.get(), 1024, 1, GBM_FORMAT_XRGB8888,
                                                     &linear, 1),
                        gbm_bo_destroy};
                    if (!bo || gbm_bo_get_modifier(bo.get()) != 0)
                        return lutils::Error{lutils::ErrorCode::Unsupported,
                                             "GBM cannot allocate explicit linear storage"};
                    fd = co::FileDescriptor{gbm_bo_get_fd(bo.get())};
                    if (fd.get() < 0)
                        return lutils::Error{lutils::ErrorCode::Unsupported,
                                             "GBM dma-buf export failed"};
                }
#endif
                auto image = im::importDmaBufImage({storage.desc, {fd.get()}, storage.planes, 0});
                if (!image)
                    return image.error();
                auto mapping = image.value().objects()[0]->map(co::Access::ReadWrite);
                if (!mapping)
                    return mapping.error();
                auto data = mapping.value()->writableData();
                if (!data)
                    return data.error();
                std::fill_n(data.value(), image.value().objects()[0]->byteCount(), std::byte{0xA5});
                std::copy(storage.bytes.begin(), storage.bytes.end(), data.value());
                auto done = mapping.value()->finish();
                if (!done)
                    return done.error();
                return image;
            };
            auto allocate = [&](Storage const &storage) -> lutils::Result<im::ImageResource> {
                if (!split)
                    return allocateOne(storage);
                std::vector<co::MemoryHandle> objects;
                auto planes = storage.planes;
                for (std::size_t p = 0; p < planes.size(); ++p) {
                    auto image = allocateOne(storage);
                    if (!image)
                        return image.error();
                    objects.push_back(image.value().objects()[0]);
                    planes[p].object = p;
                }
                return im::ImageResource::linear(storage.desc, std::move(objects),
                                                 std::move(planes));
            };
            auto verify = [&](im::ImageResource const &image, Storage const &storage,
                              std::vector<std::byte> const &oracle) {
                for (std::size_t object = 0; object < image.objects().size(); ++object) {
                    auto bytes = storage.bytes;
                    for (std::size_t p = 0; p < image.planes().size(); ++p) {
                        if (image.planes()[p].object != object)
                            continue;
                        auto const &s = spec.planes[p];
                        for (std::size_t y = 0; y < storage.desc.height / s.height; ++y)
                            for (std::size_t x = 0;
                                 x < std::size_t{storage.desc.width / s.width} * s.bytes; ++x)
                                bytes[storage.at(p, x, y)] = oracle[storage.at(p, x, y)];
                    }
                    bytes.resize(image.objects()[object]->byteCount(), std::byte{0xA5});
                    auto mapping = take(image.objects()[object]->map(co::Access::Read));
                    check(std::equal(bytes.begin(), bytes.end(), mapping->data()));
                    ok(mapping->finish());
                }
            };
            auto input = allocate(src), output = allocate(dst);
            if (!input)
                return unavailable(input.error().message);
            if (!output)
                return unavailable(output.error().message);
            Storage cropped(spec, 8, 6, 2, 3);
            auto croppedResource = allocate(cropped);
            if (!croppedResource)
                return unavailable(croppedResource.error().message);
            auto cropPlan = take(im::RegionPlan::crop(src.desc, {2, 2, 8, 6}));
            auto plan = take(im::RegionPlan::pad(cropped.desc, dst.desc, {2, 2}, spec.color));
            auto gpuInput = input, gpuOutput = output;
            auto gpuCrop = croppedResource;
            if (device) {
                gpuInput = im::bindImage(*device, input.value());
                gpuOutput = im::bindImage(*device, output.value());
                gpuCrop = im::bindImage(*device, croppedResource.value());
                if (!gpuInput)
                    return unavailable(gpuInput.error().message);
                if (!gpuOutput)
                    return unavailable(gpuOutput.error().message);
                if (!gpuCrop)
                    return unavailable(gpuCrop.error().message);
            }
            auto first = take(executor->submit(cropPlan, gpuInput.value(), gpuCrop.value()));
            auto second = take(executor->submit(plan, gpuCrop.value(), gpuOutput.value()));
            auto third = take(executor->submit(plan, gpuCrop.value(), gpuOutput.value()));
            // Release caller-side imports while queued operations still retain them.
            gpuInput = input;
            ok(third->wait());
            ok(second->wait());
            ok(first->wait());
            auto cropOracle = expected(spec, src, cropped, {2, 2, 8, 6}, {});
            verify(croppedResource.value(), cropped, cropOracle);
            verify(input.value(), src, src.bytes);
            cropped.bytes = std::move(cropOracle);
            auto oracle = expected(spec, cropped, dst, {0, 0, 8, 6}, {2, 2});
            verify(output.value(), dst, oracle);
        }
#ifdef LUTILS_TEST_REGION_DEVICE
    check(stats->stagingBuffersCreated.load() == 0 && stats->readbackVectorsCreated.load() == 0);
    executor.reset();
    device.reset();
    check(report->errors.load() == 0);
#endif
    std::cout << "real dma-buf regions passed without staging\n";
}
