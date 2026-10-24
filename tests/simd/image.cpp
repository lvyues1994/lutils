#include <algorithm>
#include <array>
#include <iostream>
#include <lutils/image/SimdConversion.hpp>
#include <stdexcept>

namespace im = lutils::image;
namespace s = lutils::simd;
void check(bool value) {
    if (!value)
        throw std::runtime_error("SIMD image mismatch");
}
void take(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
im::FrameDesc description(std::uint32_t width, std::uint32_t height) {
    return {width,
            height,
            im::uyvy422(),
            {im::Matrix::Bt709, im::Range::Limited, im::ChromaLocation::Cosited,
             im::ChromaLocation::Cosited, im::Primaries::Bt709, im::Transfer::Bt709}};
}
void conversion(std::uint32_t width, std::uint32_t height, bool reversed, bool sourceReversed,
                s::Target target, std::size_t layoutOffset = 64) {
    auto desc = description(width, height), outDesc = desc;
    outDesc.format = im::nv12();
    outDesc.color.vertical = im::ChromaLocation::Midpoint;
    // Deliberately misalign row starts and preserve padding canaries.
    std::size_t const inStride = 2 * std::size_t{width} + 7;
    std::size_t const outStride =
        layoutOffset < 32 ? (std::size_t{width} + 63) / 64 * 64 : std::size_t{width} + 5;
    std::size_t const uvRows = (std::size_t{height} + 1) / 2;
    std::vector<std::byte> input(inStride * height + 2, std::byte{0xa5});
    std::vector<std::byte> output(outStride * (height + uvRows) + 64, std::byte{0x5a});
    auto outOffset =
        layoutOffset < 32
            ? (32 + layoutOffset - reinterpret_cast<std::uintptr_t>(output.data()) % 32) % 32
            : 1;
    auto plane = [&](std::size_t offset, std::size_t rows) {
        return im::PlaneView{output.data(), output.size(),
                             offset + (reversed ? (rows - 1) * outStride : 0),
                             reversed ? -static_cast<std::ptrdiff_t>(outStride)
                                      : static_cast<std::ptrdiff_t>(outStride)};
    };
    im::FrameView source{
        desc,
        {{input.data(), input.size(), 1 + (sourceReversed ? (height - 1) * inStride : 0),
          sourceReversed ? -static_cast<std::ptrdiff_t>(inStride)
                         : static_cast<std::ptrdiff_t>(inStride)}}};
    im::FrameView destination{
        outDesc, {plane(outOffset, height), plane(outOffset + outStride * height, uvRows)}};
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < 2 * width; ++x)
            im::row(source.planes[0], y)[x] = static_cast<std::byte>((x * 23 + y * 131) % 256);
    auto const original = input;
    auto expected = output;
    for (std::size_t y = 0; y < height; ++y) {
        auto *out = im::row(destination.planes[0], y);
        auto const *in = im::row(source.planes[0], y);
        for (std::size_t x = 0; x < width; ++x)
            expected[static_cast<std::size_t>(out - output.data()) + x] = in[2 * x + 1];
    }
    for (std::size_t y = 0; y < uvRows; ++y) {
        auto *out = im::row(destination.planes[1], y);
        auto const *top = im::row(source.planes[0], 2 * y);
        auto const *bottom =
            im::row(source.planes[0], std::min(2 * y + 1, std::size_t{height} - 1));
        for (std::size_t x = 0; x < width; ++x) {
            auto average = (std::to_integer<unsigned>(top[2 * x]) +
                            std::to_integer<unsigned>(bottom[2 * x]) + 1) /
                           2;
            expected[static_cast<std::size_t>(out - output.data()) + x] =
                static_cast<std::byte>(average);
        }
    }
    take(im::convertUyvyToNv12(im::readOnly(source), destination, target));
    check(input == original && output == expected);
    auto rejected = [&](im::ConstFrameView const &a, im::FrameView const &b, s::Target requested) {
        auto result = im::convertUyvyToNv12(a, b, requested);
        check(!result);
        check(output == expected);
    };
    auto invalidSource = im::readOnly(source);
    invalidSource.desc.color.horizontal = im::ChromaLocation::Unknown;
    rejected(invalidSource, destination, target);
    invalidSource = im::readOnly(source);
    invalidSource.desc.scan = im::Scan::Interlaced;
    rejected(invalidSource, destination, target);
    invalidSource = im::readOnly(source);
    invalidSource.desc.color.vertical = im::ChromaLocation::Midpoint;
    rejected(invalidSource, destination, target);
    auto invalid = destination;
    invalid.desc.color.matrix = im::Matrix::Bt601;
    rejected(im::readOnly(source), invalid, target);
    invalid = destination;
    invalid.desc.width += 2;
    rejected(im::readOnly(source), invalid, target);
    invalid = destination;
    invalid.planes[1] = invalid.planes[0];
    rejected(im::readOnly(source), invalid, target);
    invalid = destination;
    invalid.planes[0] = {input.data(), input.size(), 1, static_cast<std::ptrdiff_t>(inStride)};
    rejected(im::readOnly(source), invalid, target);
    for (auto unavailable : {s::Target::Sse2, s::Target::Avx2, s::Target::Neon})
        if (!s::supported(unavailable))
            rejected(im::readOnly(source), destination, unavailable);
}
#include "rgba.hpp"
int main() {
    try {
        for (auto target : {s::Target::Scalar, s::Target::Sse2, s::Target::Avx2, s::Target::Neon,
                            s::Target::Automatic}) {
            if (!s::supported(target))
                continue;
            for (std::uint32_t width = 1; width <= 97; ++width)
                for (auto height : {1u, 2u, 3u, 5u})
                    for (auto reversed : {false, true})
                        rgbaConversion(width, height, reversed, target);
            rgbaConversion(1921, 17, false, target);
            for (std::size_t offset = 0; offset < 32; ++offset)
                for (auto width : {2u, 62u, 64u, 66u, 128u, 1920u})
                    for (auto height : {1u, 2u, 5u})
                        for (auto reversed : {false, true})
                            for (auto sourceReversed : {false, true})
                                conversion(width, height, reversed, sourceReversed, target, offset);
            for (std::uint32_t width = 2; width <= 130; width += 2)
                for (auto height : {1u, 2u, 3u, 5u})
                    for (auto reversed : {false, true})
                        for (auto sourceReversed : {false, true})
                            conversion(width, height, reversed, sourceReversed, target);
        }
        std::cout << "UYVY/NV12/RGBA conversion contracts passed\n";
        return 0;
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
