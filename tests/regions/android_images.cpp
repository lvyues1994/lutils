#include "Fixture.hpp"
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <lutils/compute/AndroidHardwareBufferImage.hpp>
#include <lutils/image/AndroidImage.hpp>
#include <media/NdkImageReader.h>
#include <mutex>
#ifdef LUTILS_TEST_REGION_DEVICE
#include <lutils/compute/Vulkan.hpp>
#include <lutils/image/DeviceRegion.hpp>
#endif
using namespace regions_test;
void ensure(bool condition, int line) {
    if (!condition)
        throw std::runtime_error("native image assertion at line " + std::to_string(line));
}
#define check(...) ensure((__VA_ARGS__), __LINE__)
struct Image {
    std::shared_ptr<AHardwareBuffer> buffer;
    im::ImageResource resource;
};
Image allocate(bool yuv, unsigned width, unsigned height) {
    AHardwareBuffer_Desc d{};
    d.width = width;
    d.height = height;
    d.layers = 1;
    d.format = yuv ? AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 : AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    d.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
              AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer *raw = nullptr;
    check(AHardwareBuffer_allocate(&d, &raw) == 0);
    auto owner = std::shared_ptr<AHardwareBuffer>{raw, AHardwareBuffer_release};
    return {owner, take(im::importAndroidImage(raw))};
}
unsigned char value(std::size_t plane, unsigned x, unsigned y, unsigned byte) {
    return static_cast<unsigned char>((plane * 61 + x * 7 + y * 23 + byte * 31) % 256);
}
void fill(Image const &image) {
    auto mapping = take(image.resource.nativeStorage()->map(co::Access::Write));
    auto const d = image.resource.nativeStorage()->description();
    auto const bytes = d.format == co::NativeImageFormat::Rgba8 ? 4u : 1u;
    for (std::size_t p = 0; p < mapping->planes().size(); ++p)
        std::memset(take(mapping->writablePlane(p)), 0xa5, mapping->planes()[p].capacity);
    for (std::size_t p = 0; p < mapping->planes().size(); ++p) {
        auto const &v = mapping->planes()[p];
        auto *out = take(mapping->writablePlane(p));
        for (unsigned y = 0; y < d.height / (p ? 2 : 1); ++y)
            for (unsigned x = 0; x < d.width / (p ? 2 : 1); ++x)
                for (unsigned b = 0; b < bytes; ++b)
                    out[y * v.rowStride + x * v.pixelStride + b] =
                        static_cast<std::byte>(value(p, x, y, b));
    }
    ok(mapping->finish());
}
void verify(im::ImageResource const &image, im::Rectangle rectangle, im::Point position, bool pad,
            std::vector<unsigned> const &background) {
    auto mapping = take(image.nativeStorage()->map(co::Access::Read));
    check(!mapping->writablePlane(0));
    auto const d = image.nativeStorage()->description();
    auto const bytes = d.format == co::NativeImageFormat::Rgba8 ? 4u : 1u;
    for (std::size_t p = 0; p < mapping->planes().size(); ++p) {
        auto const &v = mapping->planes()[p];
        auto const step = p ? 2u : 1u;
        for (unsigned y = 0; y < d.height / step; ++y)
            for (unsigned x = 0; x < d.width / step; ++x)
                for (unsigned b = 0; b < bytes; ++b) {
                    bool inside = x >= position.x / step && y >= position.y / step &&
                                  x - position.x / step < rectangle.width / step &&
                                  y - position.y / step < rectangle.height / step;
                    auto expected =
                        inside ? unsigned{value(p, rectangle.x / step + x - position.x / step,
                                                rectangle.y / step + y - position.y / step, b)}
                               : background[bytes == 4 ? b : p];
                    check(inside || pad);
                    auto actual =
                        std::to_integer<unsigned>(v.data[y * v.rowStride + x * v.pixelStride + b]);
                    if (actual != expected)
                        throw std::runtime_error(
                            "native pixel mismatch at plane " + std::to_string(p) + " (" +
                            std::to_string(x) + "," + std::to_string(y) + ") expected " +
                            std::to_string(expected) + " got " + std::to_string(actual));
                }
    }
    ok(mapping->finish());
}
void regions(bool yuv, co::Device *device) {
    auto source = allocate(yuv, 32, 24), crop = allocate(yuv, 16, 12),
         output = allocate(yuv, 24, 20);
    fill(source);
    fill(crop);
    fill(output);
    auto alias = take(im::importAndroidImage(source.buffer.get()));
    check(alias.aliases(source.resource));
    auto viewport =
        take(im::ImageResource::native(source.resource.nativeStorage(), {}, {2, 2, 28, 20}));
    auto cropPlan = take(im::RegionPlan::crop(viewport.description(), {2, 2, 16, 12}));
    auto background = yuv ? im::Background{{im::Component::Y, 16},
                                           {im::Component::Cb, 0},
                                           {im::Component::Cr, 255}}
                          : im::Background{{im::Component::R, 17},
                                           {im::Component::G, 35},
                                           {im::Component::B, 53},
                                           {im::Component::A, 71}};
    auto padPlan = take(im::RegionPlan::pad(crop.resource.description(),
                                            output.resource.description(), {2, 4}, background));
    auto executor = im::createCpuRegionExecutor();
#ifdef LUTILS_TEST_REGION_DEVICE
    if (device)
        executor = take(im::createDeviceRegionExecutor(*device));
#else
    (void)device;
#endif
    auto active = take(source.resource.nativeStorage()->map(co::Access::Read));
    check(!executor->submit(cropPlan, viewport, crop.resource));
    ok(active->finish());
    auto first = take(executor->submit(cropPlan, viewport, crop.resource));
    auto second = take(executor->submit(padPlan, crop.resource, output.resource));
    verify(crop.resource, {4, 4, 16, 12}, {}, false, {0, 0, 0, 0});
    verify(output.resource, {4, 4, 16, 12}, {2, 4}, true,
           yuv ? std::vector<unsigned>{16, 0, 255} : std::vector<unsigned>{17, 35, 53, 71});
    verify(source.resource, {0, 0, 32, 24}, {}, false, {0, 0, 0, 0});
    ok(second->wait());
    ok(first->wait());
    check(
        !executor->submit(take(im::RegionPlan::crop(source.resource.description(), {0, 0, 32, 24})),
                          source.resource, alias));
    // Native input can be copied to ordinary host planes without repacking a
    // whole frame.
    auto host = take(im::HostFrame::create(crop.resource.description(), 32));
    auto hostResource = take(im::ImageResource::borrow(host.view()));
    ok(im::createCpuRegionExecutor()->run(cropPlan, viewport, hostResource));
    auto planes = host.view().planes;
    for (std::size_t p = 0; p < planes.size(); ++p) {
        auto bytes = yuv ? 1u : 4u;
        auto step = p ? 2u : 1u;
        for (unsigned y = 0; y < 12 / step; ++y)
            for (unsigned x = 0; x < 16 / step; ++x)
                for (unsigned b = 0; b < bytes; ++b)
                    check(std::to_integer<unsigned>(im::row(planes[p], y)[x * bytes + b]) ==
                          value(p, 4 / step + x, 4 / step + y, b));
    }
}
void sampledRegions(co::Device &device, bool yuv) {
#ifdef LUTILS_TEST_REGION_DEVICE
    auto input = allocate(yuv, 520, 24);
    fill(input);
    auto viewport =
        take(im::ImageResource::native(input.resource.nativeStorage(), {}, {2, 2, 516, 20}));
    auto spec = formats()[yuv ? 4 : 0];
    auto executor = take(im::createDeviceRegionExecutor(device));
    for (bool pad : {false, true}) {
        auto plan = pad ? take(im::RegionPlan::pad(viewport.description(),
                                                   {532, 28, spec.format, {}}, {8, 4}, spec.color))
                        : take(im::RegionPlan::crop(viewport.description(), {2, 2, 512, 16}));
        Storage expected{spec, plan.destination().width, plan.destination().height, 1, 3};
        auto memory = take(co::AndroidHardwareBufferMemory::allocate(expected.bytes.size()));
        auto output = take(im::ImageResource::linear(expected.desc, {memory}, expected.planes));
        auto init = take(memory->map(co::Access::Write));
        std::copy(expected.bytes.begin(), expected.bytes.end(), take(init->writableData()));
        ok(init->finish());
        ok(im::createCpuRegionExecutor()->run(plan, viewport, expected.host()));
        auto pending = take(executor->submit(plan, viewport, output));
        auto mapping = take(memory->map(co::Access::Read));
        for (std::size_t i = 0; i < expected.bytes.size(); ++i)
            if (mapping->data()[i] != expected.bytes[i])
                throw std::runtime_error(
                    "sampled " + std::string(yuv ? "YUV" : "RGBA") + " byte " + std::to_string(i) +
                    " expected " + std::to_string(std::to_integer<unsigned>(expected.bytes[i])) +
                    " got " + std::to_string(std::to_integer<unsigned>(mapping->data()[i])));
        ok(mapping->finish());
        ok(pending->wait());
    }
    if (!yuv) {
        auto plan = take(im::RegionPlan::crop(viewport.description(), {1, 1, 1, 1}));
        for (std::ptrdiff_t stride : {1, 2, 3}) {
            Storage expected{spec, 1, 1, 1, 0};
            expected.planes[0].stride = stride; // One row does not use its row pitch.
            auto memory = take(co::AndroidHardwareBufferMemory::allocate(expected.bytes.size()));
            auto output = take(im::ImageResource::linear(expected.desc, {memory}, expected.planes));
            auto mapping = take(memory->map(co::Access::Write));
            std::copy(expected.bytes.begin(), expected.bytes.end(), take(mapping->writableData()));
            ok(mapping->finish());
            ok(im::createCpuRegionExecutor()->run(plan, viewport, expected.host()));
            ok(executor->run(plan, viewport, output));
            mapping = take(memory->map(co::Access::Read));
            check(std::equal(expected.bytes.begin(), expected.bytes.end(), mapping->data()));
            ok(mapping->finish());
        }
    }
    std::cout << (yuv ? "YUV" : "RGBA")
              << " native sampling matches CPU including all byte codes and "
                 "output gaps\n";
#else
    (void)device;
    (void)yuv;
#endif
}
struct Arrival {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    static void notify(void *context, AImageReader *) {
        auto &self = *static_cast<Arrival *>(context);
        {
            std::lock_guard<std::mutex> lock{self.mutex};
            self.ready = true;
        }
        self.cv.notify_one();
    }
};
void imageLease(co::Device *device) {
    Arrival arrival;
    AImageReader *raw = nullptr;
    check(AImageReader_newWithUsage(32, 24, AIMAGE_FORMAT_RGBA_8888,
                                    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                                        AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                    1, &raw) == AMEDIA_OK);
    auto reader = std::shared_ptr<AImageReader>{raw, AImageReader_delete};
    AImageReader_ImageListener listener{&arrival, Arrival::notify};
    check(AImageReader_setImageListener(raw, &listener) == AMEDIA_OK);
    ANativeWindow *window = nullptr;
    check(AImageReader_getWindow(raw, &window) == AMEDIA_OK);
    auto post = [&] {
        ANativeWindow_Buffer buffer{};
        check(ANativeWindow_lock(window, &buffer, nullptr) == 0);
        auto *bytes = static_cast<unsigned char *>(buffer.bits);
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 32; ++x)
                for (unsigned b = 0; b < 4; ++b)
                    bytes[(y * static_cast<unsigned>(buffer.stride) + x) * 4 + b] =
                        value(0, x, y, b);
        check(ANativeWindow_unlockAndPost(window) == 0);
    };
    post();
    {
        std::unique_lock<std::mutex> lock{arrival.mutex};
        check(arrival.cv.wait_for(lock, std::chrono::seconds{5}, [&] { return arrival.ready; }));
    }
    AImage *image = nullptr;
    int fence = -1;
    check(AImageReader_acquireNextImageAsync(raw, &image, &fence) == AMEDIA_OK);
    co::FileDescriptor acquire{fence};
    auto resource =
        take(im::adoptAndroidImage(im::AImageOwner{image}, {{}, reader, acquire.get()}));
    check(!resource.nativeStorage()->writable());
    check(!resource.nativeStorage()->map(co::Access::Write));
    auto output = allocate(false, 16, 12);
    auto plan = take(im::RegionPlan::crop(resource.description(), {4, 4, 16, 12}));
    auto executor = im::createCpuRegionExecutor();
#ifdef LUTILS_TEST_REGION_DEVICE
    if (device)
        executor = take(im::createDeviceRegionExecutor(*device));
#else
    (void)device;
#endif
    auto pending = take(executor->submit(plan, resource, output.resource));
    verify(output.resource, {4, 4, 16, 12}, {}, false, {0, 0, 0, 0});
    ok(pending->wait());
    post();
    AImage *blocked = nullptr;
    check(AImageReader_acquireNextImage(raw, &blocked) == AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED);
    resource = output.resource; // Return the adopted frame after all library work
                                // completed.
    pending.reset();
    AImage *next = nullptr;
    check(AImageReader_acquireNextImage(raw, &next) == AMEDIA_OK);
    im::AImageOwner nextOwner{next};
    check(AImageReader_setImageListener(raw, nullptr) == AMEDIA_OK);
    std::cout << "AImage lease, producer fence and BufferQueue release passed\n";
}
void queuedYuvImage(co::Device *device) {
    Arrival arrival;
    AImageReader *raw = nullptr;
    check(AImageReader_newWithUsage(128, 128, AIMAGE_FORMAT_YUV_420_888,
                                    AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                                        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                                        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
                                    2, &raw) == AMEDIA_OK);
    auto reader = std::shared_ptr<AImageReader>{raw, AImageReader_delete};
    AImageReader_ImageListener listener{&arrival, Arrival::notify};
    check(AImageReader_setImageListener(raw, &listener) == AMEDIA_OK);
    ANativeWindow *window = nullptr;
    check(AImageReader_getWindow(raw, &window) == AMEDIA_OK);
    ANativeWindow_Buffer queued{};
    check(ANativeWindow_lock(window, &queued, nullptr) == 0);
    check(ANativeWindow_unlockAndPost(window) == 0);
    {
        std::unique_lock<std::mutex> lock{arrival.mutex};
        check(arrival.cv.wait_for(lock, std::chrono::seconds{5}, [&] { return arrival.ready; }));
    }
    AImage *image = nullptr;
    int fd = -1;
    check(AImageReader_acquireNextImageAsync(raw, &image, &fd) == AMEDIA_OK);
    co::FileDescriptor fence{fd};
    im::AImageOwner imageOwner{image};
    AHardwareBuffer *buffer = nullptr;
    check(AImage_getHardwareBuffer(image, &buffer) == AMEDIA_OK);
    // Populate the private producer's frame while its AImage lease is held.
    // No AImage plane getter is called; those getters would retain a CPU lock.
    {
        auto writable = take(im::importAndroidImage(buffer, {{}, reader, fence.get()}));
        Image generated{std::shared_ptr<AHardwareBuffer>{}, writable};
        fill(generated);
    }
    auto resource = take(im::adoptAndroidImage(std::move(imageOwner), {{}, reader, fence.get()}));
    check(resource.nativeStorage()->description().format == co::NativeImageFormat::Yuv420);
    auto plan = take(im::RegionPlan::crop(resource.description(), {4, 6, 116, 112}));
    auto expected = take(im::HostFrame::create(plan.destination(), 32));
    ok(im::createCpuRegionExecutor()->run(plan, resource,
                                          take(im::ImageResource::borrow(expected.view()))));
    auto nativeOutput = allocate(true, 116, 112);
    ok(im::createCpuRegionExecutor()->run(plan, resource, nativeOutput.resource));
#ifdef LUTILS_TEST_REGION_DEVICE
    if (device) {
        Storage layout{formats()[4], 116, 112, 3, 5};
        auto memory = take(co::AndroidHardwareBufferMemory::allocate(layout.bytes.size()));
        auto output = take(im::ImageResource::linear(plan.destination(), {memory}, layout.planes));
        auto executor = take(im::createDeviceRegionExecutor(*device));
        auto pending = take(executor->submit(plan, resource, output));
        // The submission alone must keep the AImage lease and its reader alive.
        std::weak_ptr<AImageReader> lifetime = reader;
        resource = nativeOutput.resource;
        check(AImageReader_setImageListener(raw, nullptr) == AMEDIA_OK);
        reader.reset();
        check(!lifetime.expired());
        auto mapping = take(memory->map(co::Access::Read));
        auto shape = take(im::geometry(plan.destination()));
        for (std::size_t p = 0; p < shape.size(); ++p)
            for (std::size_t y = 0; y < shape[p].rows; ++y)
                for (std::size_t x = 0; x < shape[p].rowBytes; ++x)
                    check(mapping->data()[layout.at(p, x, y)] ==
                          im::row(expected.view().planes[p], y)[x]);
        ok(mapping->finish());
        ok(pending->wait());
        pending.reset();
        check(lifetime.expired());
    } else
#else
    (void)device;
#endif
    {
        auto mapping = take(resource.nativeStorage()->map(co::Access::Read));
        std::weak_ptr<AImageReader> lifetime = reader;
        resource = nativeOutput.resource;
        check(AImageReader_setImageListener(raw, nullptr) == AMEDIA_OK);
        reader.reset();
        check(!lifetime.expired());
        check(mapping->planes().size() == 3 && mapping->planes()[0].data);
        ok(mapping->finish());
        mapping.reset();
        check(lifetime.expired());
    }
    std::cout << "queued YUV AImage crop and retained reader lifetime passed\n";
}
int main(int argc, char **) {
    try {
        std::unique_ptr<co::Device> device;
#ifdef LUTILS_TEST_REGION_DEVICE
        auto stats = std::make_shared<co::VulkanStatistics>();
        if (argc > 1) {
            co::VulkanOptions options;
            options.requireHardware = true;
            options.statistics = stats;
            device = take(co::createVulkanDevice(options));
        }
#else
        (void)argc;
#endif
        regions(false, device.get());
        if (!device)
            regions(true, nullptr);
        if (device) {
            sampledRegions(*device, false);
            sampledRegions(*device, true);
        }
        imageLease(device.get());
        queuedYuvImage(device.get());
#ifdef LUTILS_TEST_REGION_DEVICE
        check(stats->stagingBuffersCreated.load() == 0 &&
              stats->readbackVectorsCreated.load() == 0);
#endif
        std::cout << "native AHB images passed" << (device ? ": GPU" : ": CPU") << '\n';
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
