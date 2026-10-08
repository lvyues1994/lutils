#include <add.hpp>
#include <algorithm>
#include <cstdlib>
#include <float_kernel.hpp>
#include <grouped_add.hpp>
#include <iostream>
#include <lutils/image/Conversion.hpp>
#include <packed422_to_nv12.hpp>
#include <string>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif
using namespace lutils;
namespace co = lutils::compute;
namespace im = lutils::image;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #x "\n";                              \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
template <class T> T take(Result<T> result) {
    if (!result) {
        std::cerr << result.error().message << '\n';
        std::abort();
    }
    return std::move(result).value();
}
void ok(Result<void> result) {
    if (!result) {
        std::cerr << result.error().message << '\n';
        std::abort();
    }
}
im::FrameDesc description(std::uint32_t w, std::uint32_t h, im::FormatDesc format) {
    return {w,
            h,
            std::move(format),
            {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
             im::ChromaLocation::Cosited, im::Primaries::Bt601_625, im::Transfer::Bt709},
            im::Scan::Progressive};
}
im::FrameDesc nvDescription(std::uint32_t w, std::uint32_t h) {
    auto desc = description(w, h, im::nv12());
    desc.color.vertical = im::ChromaLocation::Midpoint;
    return desc;
}
im::FrameDesc rgbDescription(std::uint32_t w, std::uint32_t h) {
    auto desc = description(w, h, im::rgba8());
    desc.color.matrix = im::Matrix::Identity;
    desc.color.range = im::Range::Full;
    desc.color.horizontal = im::ChromaLocation::Unknown;
    desc.color.vertical = im::ChromaLocation::Unknown;
    return desc;
}
void fill(im::FrameView const &frame) {
    std::uint32_t random = 1234567u;
    auto shape = take(im::geometry(frame.desc));
    for (std::size_t p = 0; p < frame.planes.size(); ++p)
        for (std::size_t y = 0; y < shape[p].rows; ++y)
            for (std::size_t x = 0; x < shape[p].rowBytes; ++x) {
                random = random * 1664525u + 1013904223u;
                im::row(frame.planes[p], y)[x] = static_cast<std::byte>(random >> 24u);
            }
}
int byte(std::byte value) { return std::to_integer<int>(value); }
void verify422(im::ConstFrameView const &source, im::ConstFrameView const &output, bool uyvy) {
    for (std::uint32_t y = 0; y < source.desc.height; ++y)
        for (std::uint32_t x = 0; x < source.desc.width; ++x)
            CHECK(im::row(output.planes[0], y)[x] ==
                  im::row(source.planes[0], y)[x * 2 + (uyvy ? 1u : 0u)]);
    for (std::uint32_t y = 0; y < (source.desc.height + 1) / 2; ++y) {
        auto top = im::row(source.planes[0], y * 2);
        auto bottom = im::row(source.planes[0], std::min(y * 2 + 1, source.desc.height - 1));
        for (std::uint32_t x = 0; x < source.desc.width; ++x)
            CHECK(
                byte(im::row(output.planes[1], y)[x]) ==
                (byte(top[x * 2 + (uyvy ? 0u : 1u)]) + byte(bottom[x * 2 + (uyvy ? 0u : 1u)]) + 1) /
                    2);
    }
}
void verifyRgb(im::ConstFrameView const &source, im::ConstFrameView const &output) {
    auto quantize = [](int v) { return std::clamp((v + 128) / 256, 0, 255); };
    for (std::uint32_t y = 0; y < source.desc.height; ++y) {
        for (std::uint32_t x = 0; x < source.desc.width; ++x) {
            int luma = byte(im::row(source.planes[0], y)[x]) - 16;
            int u = byte(im::row(source.planes[1], y / 2)[x / 2 * 2]) - 128;
            int v = byte(im::row(source.planes[1], y / 2)[x / 2 * 2 + 1]) - 128;
            auto rgb = im::row(output.planes[0], y) + x * 4;
            CHECK(byte(rgb[0]) == quantize(298 * luma + 409 * v));
            CHECK(byte(rgb[1]) == quantize(298 * luma - 100 * u - 208 * v));
            CHECK(byte(rgb[2]) == quantize(298 * luma + 516 * u));
            CHECK(byte(rgb[3]) == 255);
        }
    }
}
void runtime(co::Device &device) {
    auto kernel = take(device.createKernel(lutils::generated::add()));
    auto input = take(device.createBuffer(4));
    auto output = take(device.createBuffer(4));
    ok(device.upload(input, {1, 2, 3, 4}));
    co::CommandList list;
    ok(list.dispatch({kernel, {input, output}, {4, 3}, {4, 1, 1}}));
    auto done = take(device.submit(list));
    ok(done->wait());
    CHECK(take(done->ready()));
    CHECK(take(device.download(output)) == std::vector<co::Word>({5, 7, 9, 11}));
    co::CommandList second;
    ok(second.dispatch({kernel, {output, input}, {4, 1}, {4, 1, 1}}));
    auto next = take(device.submit(second));
    second = {};
    list = {};
    done.reset();
    next.reset();
    kernel.reset();
    output.reset();
    CHECK(take(device.download(input)) == std::vector<co::Word>({11, 15, 19, 23}));
    // Keep only weak references after submission. Queue owns all needed
    // resources.
    auto k = take(device.createKernel(lutils::generated::add()));
    auto a = take(device.createBuffer(4));
    auto b = take(device.createBuffer(4));
    std::weak_ptr<co::Buffer> retained = a;
    co::CommandList pending;
    ok(pending.dispatch({k, {a, b}, {4, 0}, {4, 1, 1}}));
    auto completion = take(device.submit(pending));
    pending = {};
    a.reset();
    k.reset();
    completion.reset();
    (void)take(device.download(b));
    CHECK(retained.expired());
    auto foreignDevice = take(co::createCpuDevice());
    auto foreign = take(foreignDevice->createBuffer(4));
    k = take(device.createKernel(lutils::generated::add()));
    co::CommandList bad;
    CHECK(!bad.dispatch({k, {b, b}, {4, 0}, {4, 1, 1}}));
    CHECK(!bad.dispatch({k, {b, input}, {4}, {4, 1, 1}}));
    ok(bad.dispatch({k, {foreign, b}, {4, 0}, {4, 1, 1}}));
    CHECK(!device.submit(bad));
    auto source = lutils::generated::add();
    source.localSize.x = 0;
    CHECK(!device.createKernel(source));
    source.localSize = {1, 2, 1};
    CHECK(!device.createKernel(source));
    CHECK(!device.upload(input, {1}));
    auto floatKernel = take(device.createKernel(lutils::generated::float_kernel()));
    co::CommandList floating;
    auto parameters = lutils::generated::pack_float_kernel({0.5f, 3, 4});
    ok(floating.dispatch({floatKernel, {input, b}, parameters, {4, 1, 1}}));
    auto floatDone = take(device.submit(floating));
    ok(floatDone->wait());
    CHECK(take(device.download(b)) == std::vector<co::Word>({8, 10, 12, 14}));
}
void workgroups(co::Device &device, bool vulkan) {
    auto source = lutils::generated::grouped_add();
    CHECK(source.localSize.x == 4 && source.localSize.y == 1 && source.localSize.z == 1);
    auto kernel = take(device.createKernel(source));
    auto input = take(device.createBuffer(8));
    auto output = take(device.createBuffer(8));
    ok(device.upload(input, {0, 1, 2, 3, 4, 5, 6, 7}));
    ok(device.upload(output, std::vector<co::Word>(8, 99)));
    co::CommandList commands;
    CHECK(!commands.dispatch({kernel, {input, output}, {7, 5}, {7, 1, 1}}));
    ok(commands.dispatch({kernel, {input, output}, {7, 5}, {8, 1, 1}}));
    auto done = take(device.submit(commands));
    ok(done->wait());
    CHECK(take(device.download(output)) == std::vector<co::Word>({5, 7, 9, 11, 13, 15, 17, 99}));
    source.localSize.x = 2;
    CHECK(co::validate(source));
    CHECK(bool(device.createKernel(source)) == !vulkan);
}
void largeInvocationIndex(co::Device &device) {
    auto desc = description(2, 1, im::uyvy422());
    auto outputDesc = nvDescription(2, 1);
    auto source = take(im::DeviceFrame::create(device, desc));
    auto target = take(im::DeviceFrame::create(device, outputDesc));
    auto plan = take(im::ConversionPlan::prepare(device, desc, outputDesc));
    co::CommandList commands;
    ok(plan.record(commands, source, target));
    auto const &dispatch = std::get<co::Dispatch>(commands.commands().front());
    co::Word input = 0, output = 99;
    CHECK(dispatch.kernel->source().abiVersion == 2);
    im::kernels::Packed422ToNv12 kernel;
    kernel.parameters =
        co::StorageCodec<im::kernels::Packed422Params>::read(dispatch.parameters.data() + 4);
    kernel.source.cpuView(&input, 1);
    kernel.destination.cpuView(&output, 1);
    co::BuiltinScope scope;
    co::kernel::gl_GlobalInvocationID = {0x40000000u, 0u, 0u};
    kernel.main();
    CHECK(output == 99);
}
void conversions(co::Device &device, std::uint32_t width, std::uint32_t height, bool uyvy) {
    auto src = take(im::HostFrame::create(
        description(width, height, uyvy ? im::uyvy422() : im::yuyv422()), 16));
    auto nv = take(im::HostFrame::create(nvDescription(width, height), 16));
    for (auto const &plane : nv.view().planes)
        if (plane.capacity)
            std::fill_n(plane.base, plane.capacity, std::byte{0xA5});
    auto rgb = take(im::HostFrame::create(rgbDescription(width, height), 16));
    fill(src.view());
    auto sourceView = src.view();
    if (height) {
        sourceView.planes[0].row0 =
            static_cast<std::size_t>(sourceView.planes[0].stride) * (height - 1);
        sourceView.planes[0].stride = -sourceView.planes[0].stride;
    }
    auto source = im::readOnly(sourceView);
    auto first = take(im::ConversionPlan::prepare(device, source.desc, nv.description()));
    auto second = take(im::ConversionPlan::prepare(device, nv.description(), rgb.description()));
    auto gpuSrc = take(im::DeviceFrame::create(device, source.desc));
    auto gpuNv = take(im::DeviceFrame::create(device, nv.description()));
    auto gpuRgb = take(im::DeviceFrame::create(device, rgb.description()));
    ok(im::upload(device, source, gpuSrc));
    co::CommandList commands;
    ok(first.record(commands, gpuSrc, gpuNv));
    ok(second.record(commands, gpuNv, gpuRgb));
    auto done = take(device.submit(commands));
    ok(done->wait());
    ok(im::download(device, gpuNv, nv.view()));
    ok(im::download(device, gpuRgb, rgb.view()));
    verify422(source, im::readOnly(nv.view()), uyvy);
    auto shapes = take(im::geometry(nv.description()));
    for (std::size_t p = 0; p < shapes.size(); ++p) {
        auto plane = nv.view().planes[p];
        for (std::size_t y = 0; y < shapes[p].rows; ++y)
            for (std::size_t x = shapes[p].rowBytes; x < static_cast<std::size_t>(plane.stride);
                 ++x)
                CHECK(im::row(plane, y)[x] == std::byte{0xA5});
    }
    verifyRgb(im::readOnly(nv.view()), im::readOnly(rgb.view()));
    auto wrong = nv.description();
    wrong.color.matrix = im::Matrix::Bt709;
    CHECK(!im::ConversionPlan::prepare(device, wrong, rgb.description()));
}
void oddRgb(co::Device &device) {
    auto nv = take(im::HostFrame::create(nvDescription(3, 3), 8));
    fill(nv.view());
    auto rgb = take(im::HostFrame::create(rgbDescription(3, 3), 16));
    auto target = rgb.view();
    std::fill(target.planes[0].base, target.planes[0].base + target.planes[0].capacity,
              std::byte{0xA5});
    auto plan = take(im::ConversionPlan::prepare(device, nv.description(), rgb.description()));
    ok(plan.run(device, im::readOnly(nv.view()), target));
    verifyRgb(im::readOnly(nv.view()), im::readOnly(target));
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 12; x < 16; ++x)
            CHECK(im::row(target.planes[0], y)[x] == std::byte{0xA5});
}
void fixedCases(co::Device &device) {
    auto yuyv = take(im::HostFrame::create(description(2, 2, im::yuyv422())));
    auto nv = take(im::HostFrame::create(nvDescription(2, 2)));
    std::vector<unsigned> input{10, 21, 30, 40, 50, 60, 70, 81};
    for (std::size_t i = 0; i < input.size(); ++i)
        yuyv.view().planes[0].base[i] = static_cast<std::byte>(input[i]);
    auto first = take(im::ConversionPlan::prepare(device, yuyv.description(), nv.description()));
    ok(first.run(device, im::readOnly(yuyv.view()), nv.view()));
    auto view = nv.view();
    CHECK(byte(view.planes[0].base[0]) == 10 && byte(view.planes[0].base[1]) == 30);
    CHECK(byte(view.planes[0].base[2]) == 50 && byte(view.planes[0].base[0 + 3]) == 70);
    CHECK(byte(view.planes[1].base[0]) == 41 && byte(view.planes[1].base[1]) == 61);
    auto uyvy = take(im::HostFrame::create(description(2, 2, im::uyvy422())));
    std::vector<unsigned> uyvyInput{21, 10, 40, 30, 60, 50, 81, 70};
    for (std::size_t i = 0; i < uyvyInput.size(); ++i)
        uyvy.view().planes[0].base[i] = static_cast<std::byte>(uyvyInput[i]);
    auto uyvyPlan = take(im::ConversionPlan::prepare(device, uyvy.description(), nv.description()));
    ok(uyvyPlan.run(device, im::readOnly(uyvy.view()), nv.view()));
    CHECK(byte(view.planes[0].base[0]) == 10 && byte(view.planes[0].base[1]) == 30);
    CHECK(byte(view.planes[0].base[2]) == 50 && byte(view.planes[0].base[3]) == 70);
    CHECK(byte(view.planes[1].base[0]) == 41 && byte(view.planes[1].base[1]) == 61);
    auto single = take(im::HostFrame::create(nvDescription(1, 1)));
    auto rgb = take(im::HostFrame::create(rgbDescription(1, 1)));
    auto plan = take(im::ConversionPlan::prepare(device, single.description(), rgb.description()));
    auto pixel = single.view();
    pixel.planes[1].base[0] = std::byte{128};
    pixel.planes[1].base[1] = std::byte{128};
    for (auto level : {16, 235}) {
        pixel.planes[0].base[0] = static_cast<std::byte>(level);
        ok(plan.run(device, im::readOnly(pixel), rgb.view()));
        auto result = rgb.view().planes[0].base;
        CHECK(byte(result[0]) == (level == 16 ? 0 : 255));
        CHECK(result[0] == result[1] && result[1] == result[2]);
        CHECK(byte(result[3]) == 255);
    }
    pixel.planes[0].base[0] = std::byte{81};
    pixel.planes[1].base[0] = std::byte{90};
    pixel.planes[1].base[1] = std::byte{240};
    ok(plan.run(device, im::readOnly(pixel), rgb.view()));
    CHECK(byte(rgb.view().planes[0].base[0]) == 255 && byte(rgb.view().planes[0].base[1]) == 0 &&
          byte(rgb.view().planes[0].base[2]) == 0);
    auto bad = single.description();
    bad.scan = im::Scan::Interlaced;
    CHECK(!im::ConversionPlan::prepare(device, bad, rgb.description()));
    bad = single.description();
    bad.color.horizontal = im::ChromaLocation::Unknown;
    CHECK(!im::ConversionPlan::prepare(device, bad, rgb.description()));
    bad = single.description();
    bad.color.transfer = im::Transfer::Linear;
    CHECK(!im::ConversionPlan::prepare(device, bad, rgb.description()));
}
void genericTransfer(co::Device &device) {
    im::FormatDesc rgb24{"RGB24",
                         {{1,
                           1,
                           3,
                           {{im::Component::R, 0, 0, 0, 0, 8},
                            {im::Component::G, 0, 0, 8, 0, 8},
                            {im::Component::B, 0, 0, 16, 0, 8}}}},
                         1,
                         1,
                         {{im::Component::R}, {im::Component::G}, {im::Component::B}}};
    auto desc = description(3, 3, rgb24);
    std::vector<std::byte> input(40, std::byte{0xEE});
    std::vector<std::byte> output(40, std::byte{0xA5});
    im::FrameView src{desc, {{input.data(), input.size(), 1, 13}}};
    im::FrameView dst{desc, {{output.data(), output.size(), 27, -13}}};
    fill(src);
    auto frame = take(im::DeviceFrame::create(device, desc));
    ok(im::upload(device, im::readOnly(src), frame));
    ok(im::download(device, frame, dst));
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 0; x < 9; ++x)
            CHECK(im::row(src.planes[0], y)[x] == im::row(dst.planes[0], y)[x]);
    for (auto i : {0u, 10u, 11u, 12u, 13u, 23u, 24u, 25u, 26u, 36u, 37u, 38u, 39u})
        CHECK(output[i] == std::byte{0xA5});
    auto huge = description(UINT32_MAX - 1, UINT32_MAX, im::rgba8());
    CHECK(!im::deviceLayout(huge));
}
int main(int argc, char **argv) {
    bool vulkan = argc > 1 && std::string{argv[1]} == "vulkan";
    std::unique_ptr<co::Device> device;
#ifdef LUTILS_TEST_VULKAN
    co::VulkanOptions options;
    options.validationReport = std::make_shared<co::ValidationReport>();
    for (int i = 2; i < argc; ++i) {
        auto arg = std::string{argv[i]};
        if (arg == "--require-hardware")
            options.requireHardware = true;
        else if (arg == "--validation")
            options.enableValidation = true;
        else if (arg == "--timestamps")
            options.enableTimestamps = true;
        else if (arg == "--host-cached")
            options.preferHostCached = true;
        else if (arg == "--device-local")
            options.deviceLocal = true;
        else
            CHECK(false);
    }
    if (vulkan)
        device = take(co::createVulkanDevice(options));
#else
    CHECK(!vulkan);
#endif
    if (!device)
        device = take(co::createCpuDevice());
    std::cout << device->info().name << '\n';
    runtime(*device);
    workgroups(*device, vulkan);
    largeInvocationIndex(*device);
    for (bool uyvy : {false, true}) {
        conversions(*device, 2, 1, uyvy);
        conversions(*device, 2, 2, uyvy);
        conversions(*device, 6, 3, uyvy);
        conversions(*device, 124, 3, uyvy);
        conversions(*device, 128, 3, uyvy);
        conversions(*device, 130, 17, uyvy);
        conversions(*device, 252, 3, uyvy);
        conversions(*device, 256, 3, uyvy);
        conversions(*device, 258, 3, uyvy);
        conversions(*device, 0, 0, uyvy);
    }
    oddRgb(*device);
    fixedCases(*device);
    genericTransfer(*device);
#ifdef LUTILS_TEST_VULKAN
    if (vulkan) {
        auto done = take(device->submit(co::CommandList{}));
        auto *timed = dynamic_cast<co::VulkanCompletion *>(done.get());
        CHECK(timed);
        auto elapsed = timed->elapsedNanoseconds();
        CHECK(bool(elapsed) == options.enableTimestamps);
        if (elapsed)
            CHECK(elapsed.value() >= 0);
        else
            CHECK(elapsed.error().code == ErrorCode::Unsupported);
    }
    device.reset();
    CHECK(options.validationReport->errors.load() == 0);
    if (options.enableValidation)
        std::cout << "validation errors=" << options.validationReport->errors.load()
                  << " warnings=" << options.validationReport->warnings.load() << '\n';
#endif
    std::cout << "runtime and conversion contracts passed\n";
}
