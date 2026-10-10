#include "../regions/Fixture.hpp"
#include <lutils/image/DeviceRegion.hpp>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif

using namespace regions_test;
int main(int argc, char **argv) {
    std::unique_ptr<co::Device> device;
#ifdef LUTILS_TEST_VULKAN
    auto statistics = std::make_shared<co::VulkanStatistics>();
    auto report = std::make_shared<co::ValidationReport>();
    if (argc > 1 && std::string{argv[1]} == "vulkan") {
        co::VulkanOptions options;
        options.statistics = statistics;
        options.validationReport = report;
        options.enableValidation = argc > 2;
        device = take(co::createVulkanDevice(options));
    }
#else
    (void)argc;
    (void)argv;
#endif
    if (!device)
        device = take(co::createCpuDevice());
    std::cout << device->info().name << " dma-buf=" << device->info().capabilities.externalDmaBuf
              << '\n';
    auto executor = take(im::createDeviceRegionExecutor(*device));
    for (auto const &spec : formats())
        for (unsigned offset = 0; offset < 4; ++offset)
            for (bool padding : {false, true}) {
                Storage src(spec, 12, 10, offset, offset);
                Storage dst(spec, padding ? 16u : 8u, padding ? 12u : 6u, 3 - offset, 3 - offset);
                src.fill(spec);
                im::Rectangle rectangle =
                    padding ? im::Rectangle{0, 0, 12, 10} : im::Rectangle{2, 2, 8, 6};
                im::Point position = padding ? im::Point{2, 2} : im::Point{};
                auto oracle = expected(spec, src, dst, rectangle, position);
                auto plan =
                    take(padding ? im::RegionPlan::pad(src.desc, dst.desc, position, spec.color)
                                 : im::RegionPlan::crop(src.desc, rectangle));
                auto input = src.resident(*device), output = dst.resident(*device);
#ifdef LUTILS_TEST_VULKAN
                auto staging = statistics->stagingBuffersCreated.load();
                auto readbacks = statistics->readbackVectorsCreated.load();
#endif
                auto first = take(executor->submit(plan, input, output));
                auto second = take(executor->submit(plan, input, output));
                ok(second->wait());
                ok(first->wait());
#ifdef LUTILS_TEST_VULKAN
                check(statistics->stagingBuffersCreated.load() == staging);
                check(statistics->readbackVectorsCreated.load() == readbacks);
#endif
                check(download(*device, output) == oracle);
                check(download(*device, input) == src.bytes);
            }
    auto spec = formats()[0];
    for (std::ptrdiff_t stride : {1, 2, 3}) {
        Storage one(spec, 1, 1, 0, 0), target(spec, 1, 1, 0, 0);
        one.fill(spec);
        one.planes[0].stride = target.planes[0].stride = stride;
        auto in = one.resident(*device), out = target.resident(*device);
        auto single = take(im::RegionPlan::crop(one.desc, {0, 0, 1, 1}));
        ok(executor->run(single, in, out));
        check(download(*device, out) == expected(spec, one, target, {0, 0, 1, 1}, {}));
    }
    Storage src(spec, 260, 260, 1, 3), dst(spec, 264, 264, 3, 1);
    src.fill(spec);
    auto plan = take(im::RegionPlan::pad(src.desc, dst.desc, {2, 2}, spec.color));
    auto output = dst.resident(*device);
    // Temporary imported handles survive until completion.
    auto done = take(executor->submit(plan, src.resident(*device), output));
    ok(done->wait());
    check(download(*device, output) == expected(spec, src, dst, {0, 0, 260, 260}, {2, 2}));
    check(!executor->submit(plan, src.host(), dst.host())); // no host upload fallback
    auto input = src.resident(*device);
    auto crop = take(im::RegionPlan::crop(src.desc, {0, 0, 260, 260}));
    check(!executor->submit(crop, input, input));
    auto shortBuffer = take(device->createBufferBytes(src.bytes.size() - 1));
    auto shortImage = take(
        im::ImageResource::linear(src.desc, {take(co::bufferMemory(shortBuffer))}, src.planes));
    check(!executor->submit(crop, shortImage, input));
    executor.reset();
    device.reset();
#ifdef LUTILS_TEST_VULKAN
    check(report->errors.load() == 0);
    std::cout << "validation errors=" << report->errors.load() << '\n';
#endif
    std::cout << "device regions match independent byte oracle; no operation staging\n";
}
