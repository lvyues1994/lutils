#include "Fixture.hpp"
#include <android/hardware_buffer.h>
#include <fcntl.h>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <sys/socket.h>
#ifdef LUTILS_TEST_REGION_DEVICE
#include <lutils/compute/Vulkan.hpp>
#include <lutils/image/DeviceRegion.hpp>
#endif

using namespace regions_test;
using Ahb = co::AndroidHardwareBufferMemory;

im::ImageResource allocate(Storage const &storage, bool split) {
    std::vector<co::MemoryHandle> objects;
    auto planes = storage.planes;
    for (std::size_t i = 0; i < (split ? planes.size() : 1); ++i) {
        auto memory = take(Ahb::allocate(storage.bytes.size()));
        auto mapping = take(memory->map(co::Access::Write));
        std::copy(storage.bytes.begin(), storage.bytes.end(), take(mapping->writableData()));
        ok(mapping->finish());
        check(mapping->data() == nullptr);
        objects.push_back(memory);
    }
    if (split)
        for (std::size_t i = 0; i < planes.size(); ++i)
            planes[i].object = i;
    return take(im::ImageResource::linear(storage.desc, std::move(objects), std::move(planes)));
}
void verify(im::ImageResource const &image, Storage const &before,
            std::vector<std::byte> const &after) {
    auto shape = take(im::geometry(image.description()));
    for (std::size_t object = 0; object < image.objects().size(); ++object) {
        auto oracle = before.bytes;
        for (std::size_t p = 0; p < image.planes().size(); ++p) {
            auto const &plane = image.planes()[p];
            if (plane.object != object)
                continue;
            for (std::size_t y = 0; y < shape[p].rows; ++y)
                for (std::size_t x = 0; x < shape[p].rowBytes; ++x)
                    oracle[before.at(p, x, y)] = after[before.at(p, x, y)];
        }
        auto mapping = take(image.objects()[object]->map(co::Access::Read));
        check(!mapping->writableData());
        check(std::equal(oracle.begin(), oracle.end(), mapping->data()));
        ok(mapping->finish());
    }
}
void ownership() {
    check(!Ahb::import(nullptr));
    check(!Ahb::allocate(0));
    auto memory = take(Ahb::allocate(4096));
    auto alias = take(Ahb::import(memory->nativeBuffer()));
    check(alias->aliases(*memory));
    co::FileDescriptor ordinary{open("/dev/null", O_RDONLY | O_CLOEXEC)};
    check(!memory->importSyncFile(ordinary.get(), co::Access::Read));
    ok(memory->importSyncFile(-1, co::Access::Read));
    auto mapping = take(memory->map(co::Access::ReadWrite));
    check(!alias->map(co::Access::Read));
    check(!alias->exportSyncFile(co::Access::Read));
    memory.reset();
    // A mapping keeps both native storage and canonical synchronization state alive.
    auto reimport = take(Ahb::import(alias->nativeBuffer()));
    alias.reset();
    check(!reimport->map(co::Access::Write));
    ok(mapping->finish());
    auto second = take(reimport->map(co::Access::Read));
    ok(second->finish());

    int sockets[2] = {-1, -1};
    check(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    co::FileDescriptor sender{sockets[0]}, receiver{sockets[1]};
    check(AHardwareBuffer_sendHandleToUnixSocket(reimport->nativeBuffer(), sender.get()) == 0);
    AHardwareBuffer *received = nullptr;
    check(AHardwareBuffer_recvHandleFromUnixSocket(receiver.get(), &received) == 0);
    auto native = std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)>{
        received, AHardwareBuffer_release};
    auto ipc = take(Ahb::import(native.get()));
    check(ipc->aliases(*reimport));
    auto active = take(ipc->map(co::Access::Read));
    check(!reimport->exportSyncFile(co::Access::Read));
    ok(active->finish());

    AHardwareBuffer_Desc desc{};
    desc.width = 16;
    desc.height = 16;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    AHardwareBuffer *rgba = nullptr;
    check(AHardwareBuffer_allocate(&desc, &rgba) == 0);
    auto rgbaOwner = std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)>{
        rgba, AHardwareBuffer_release};
    check(!Ahb::import(rgbaOwner.get()));
}
int main(int argc, char **argv) {
    try {
        bool vulkan = false, strict = false;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--vulkan")
                vulkan = true;
            else if (option == "--require-supported")
                strict = true;
            else
                throw std::runtime_error("unknown option");
        }
        if (!Ahb::available()) {
            std::cout << "AHardwareBuffer stable IDs require Android API 31\n";
            return strict ? 1 : 77;
        }
        ownership();
        std::unique_ptr<co::Device> device;
        auto executor = im::createCpuRegionExecutor();
#ifdef LUTILS_TEST_REGION_DEVICE
        auto stats = std::make_shared<co::VulkanStatistics>();
        if (vulkan) {
            co::VulkanOptions options;
            options.requireHardware = true;
            options.statistics = stats;
            device = take(co::createVulkanDevice(options));
            std::cout << device->info().name
                      << " AHB=" << device->info().capabilities.externalAndroidHardwareBuffer
                      << '\n';
            if (!device->info().capabilities.externalAndroidHardwareBuffer)
                return strict ? 1 : 77;
            executor = take(im::createDeviceRegionExecutor(*device));
        }
#else
        if (vulkan)
            return strict ? 1 : 77;
#endif
        for (auto const &spec : formats())
            for (bool split : {false, true}) {
                Storage src(spec, 12, 10, 1, 3), crop(spec, 8, 6, 3, 1), dst(spec, 16, 12, 2, 2);
                src.fill(spec);
                auto originalCrop = crop;
                crop.bytes = expected(spec, src, crop, {2, 2, 8, 6}, {});
                auto padded = expected(spec, crop, dst, {0, 0, 8, 6}, {2, 2});
                auto input = allocate(src, split), middle = allocate(originalCrop, split),
                     output = allocate(dst, split);
                auto cropPlan = take(im::RegionPlan::crop(src.desc, {2, 2, 8, 6}));
                auto padPlan = take(im::RegionPlan::pad(crop.desc, dst.desc, {2, 2}, spec.color));
                std::vector<std::shared_ptr<co::Completion>> pending;
                {
                    auto in = input, mid = middle, out = output;
                    if (device) {
                        in = take(im::bindImage(*device, input));
                        mid = take(im::bindImage(*device, middle));
                        out = take(im::bindImage(*device, output));
                    }
                    for (int repeat = 0; repeat < 2; ++repeat) {
                        pending.push_back(take(executor->submit(cropPlan, in, mid)));
                        pending.push_back(take(executor->submit(padPlan, mid, out)));
                    }
                }
                // No Completion wait: CPU map must wait the published GPU fence itself.
                verify(output, dst, padded);
                verify(middle, originalCrop, crop.bytes);
                verify(input, src, src.bytes);
                for (auto it = pending.rbegin(); it != pending.rend(); ++it)
                    ok((*it)->wait());
            }
#ifdef LUTILS_TEST_REGION_DEVICE
        check(stats->stagingBuffersCreated.load() == 0);
        check(stats->readbackVectorsCreated.load() == 0);
#endif
        std::cout << "AHB crop/pad: five formats, shared/separate planes, guards, fences and "
                     "lifetime passed\n";
        return 0;
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
