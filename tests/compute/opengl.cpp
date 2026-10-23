#include "TestDevice.hpp"
#include <add.hpp>
#include <cstring>
#include <iostream>
#include <lutils/compute/Memory.hpp>
#include <thread>
namespace co = lutils::compute;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
void check(bool value) {
    if (!value)
        throw std::runtime_error("GL contract mismatch");
}
int main(int argc, char **argv) {
    try {
        auto device = take(test_device::createGl(argc, argv, 1));
        auto other = take(test_device::createGl(argc, argv));
        std::cout << device->info().name << '\n';
        // A second device must not interfere with the first device/context.
        auto input = take(device->createBufferBytes(6));
        check(input->wordCount() == 2 && input->byteCount() == 6);
        take(device->upload(input, {0x12345678u, 0xabcdef90u}));
        auto held = take(other->createBuffer(1));
        other.reset();
        std::thread destroy{[buffer = std::move(held)]() mutable { buffer.reset(); }};
        destroy.join();
        auto original = take(device->download(input));
        check(original == std::vector<co::Word>({0x12345678u, 0xabcdef90u}));
        auto another = take(device->download(input));
        another[0] = 0;
        check(original[0] == 0x12345678u && take(device->download(input)) == original);
        auto source = lutils::generated::add();
        auto missing = source;
        missing.glsl.clear();
        auto rejected = device->createKernel(missing);
        check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
        auto broken = source;
        for (auto &artifact : broken.glsl)
            artifact.text = "#version 310 es\nthis is invalid;";
        rejected = device->createKernel(broken);
        check(!rejected && rejected.error().code == lutils::ErrorCode::Compilation &&
              !rejected.error().message.empty());
        auto kernel = take(device->createKernel(source));
        auto output = take(device->createBuffer(2));
        co::CommandList emptyGrid;
        take(emptyGrid.dispatch({kernel, {input, output}, {2, 1}, {0, 1, 1}}));
        take(take(device->submit(emptyGrid))->wait());
        check(take(device->download(output)) == std::vector<co::Word>(2));
        for (co::Word i = 0; i < 6; ++i) {
            co::CommandList list;
            take(list.upload(input, {i, i + 1}));
            take(list.dispatch({kernel, {input, output}, {2, 7}, {2, 1, 1}}));
            (void)take(device->submit(list)); // Intentionally drop the completion.
        }
        check(take(device->download(output)) == std::vector<co::Word>({17, 19}));
        take(take(device->submit({}))->wait());
        using F = co::kernel::ImageFormat;
        for (auto format : {F::RGBA8Snorm, F::RGBA16F, F::RGBA32F, F::R32F, F::RGBA8UI}) {
            co::ImageDesc desc{format, 2, {3, 1, 1}};
            auto image = take(device->createImage(desc));
            std::vector<co::Word> values(image->wordCount());
            co::Word patterns[]{0x80808080u, 0x7c017e01u, 0xffc12345u,
                                0x80000000u, 0x7f800001u, 0x01234567u};
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = patterns[i % 6];
            take(device->upload(image, values));
            auto actual = take(device->download(image));
            if (actual != values)
                throw std::runtime_error(std::string{"raw image bits changed: "} +
                                         co::kernel::formatInfo(format).glsl);
        }
        // Ordinary host pointers have no implicit staging/import path.
        std::byte host[8]{};
        check(!device->importMemory(take(co::borrowMemory(host, sizeof(host)))));
        co::OpenGlOptions invalid;
        invalid.maxInFlight = 0;
        check(!co::createOpenGlDevice(invalid));
        std::cout << "GL isolation, rejection, raw storage, empty grids and "
                     "dropped completions passed\n";
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
