#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <lutils/compute/Compute.hpp>
#include <stdexcept>
#include <vector>

namespace pixel_download_test {
namespace co = lutils::compute;
namespace k = co::kernel;
inline void require(bool value) {
    if (!value)
        throw std::runtime_error("bulk pixel download changed conversion semantics");
}
template <k::ImageFormat G, class P> void compare(unsigned char const *bytes, std::size_t count) {
    using F = k::GPUFormatTraits<G>;
    std::vector<P> actual(count + 2), expected(count);
    auto first = actual.front(), last = actual.back();
    for (std::size_t i = 0; i < count; ++i)
        co::decodePixel<G>(expected[i], F::load(bytes + i * F::bytes));
    co::detail::decodeImagePixels<G>(actual.data() + 1, bytes, count);
    for (std::size_t i = 0; i < count; ++i)
        require(std::memcmp(actual[i + 1].data.data(), expected[i].data.data(),
                            sizeof(expected[i].data)) == 0);
    require(actual.front().data == first.data && actual.back().data == last.data);
}
template <k::ImageFormat G> void format() {
    using F = k::GPUFormatTraits<G>;
    using T = typename F::Storage;
    constexpr auto order = F::channels == 1   ? k::ChannelOrder::R
                           : F::channels == 2 ? k::ChannelOrder::RG
                                              : k::ChannelOrder::RGBA;
    using P = k::Pixel<T, order, F::kind>;
    co::detail::decodeImagePixels<G>(static_cast<P *>(nullptr), nullptr, 0);
    std::array<std::uint32_t, 8> patterns{0,           UINT32_MAX,  0x80808080u, 0x7f800001u,
                                          0xff800000u, 0x80000000u, 0x7e017c01u, 0x01234567u};
    constexpr std::size_t count = 257;
    std::vector<unsigned char> storage(count * F::bytes + 1);
    auto input = storage.data() + 1; // Test unaligned input and non-vector-sized tails.
    for (std::size_t i = 0; i < count * F::bytes; ++i)
        input[i] = static_cast<unsigned char>(patterns[(i / 4) % patterns.size()] >> (8 * (i % 4)));
    for (auto n : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{17}, count}) {
        compare<G, P>(input, n);
        if constexpr (F::channels == 4) {
            compare<G, k::Pixel<T, k::ChannelOrder::BGRA, F::kind>>(input, n);
            compare<G, k::Pixel<T, k::ChannelOrder::BGR, F::kind>>(input, n);
        }
    }
}
template <k::ImageFormat G> void normalizedCodes() {
    using F = k::GPUFormatTraits<G>;
    using T = typename F::Storage;
    auto count = std::size_t{std::numeric_limits<T>::max()} + 1;
    std::vector<unsigned char> storage(count * F::bytes + 1);
    auto input = storage.data() + 1;
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t c = 0; c < F::channels; ++c) {
            auto value = static_cast<T>(i * (2 * c + 1) + c * 37);
            std::memcpy(input + i * F::bytes + c * sizeof(T), &value, sizeof(T));
        }
    compare<G, k::Pixel<T, k::ChannelOrder::RGBA, k::NumberKind::UNorm>>(input, count);
    compare<G, k::Pixel<T, k::ChannelOrder::BGRA, k::NumberKind::UNorm>>(input, count);
}
inline void run() {
#define LUTILS_TEST_PIXEL_DOWNLOAD(N, G, T, K, C, V) format<k::ImageFormat::N>();
    LUTILS_IMAGE_FORMATS(LUTILS_TEST_PIXEL_DOWNLOAD)
#undef LUTILS_TEST_PIXEL_DOWNLOAD
    normalizedCodes<k::gpu::RGBA8>();
    normalizedCodes<k::gpu::RGBA16>();
    // Cross-encoding and missing channels retain their numerical conversions.
    unsigned char bytes[]{0, 128, 255, 127};
    compare<k::gpu::RGBA8, k::cpu::RGBA8UI>(bytes, 1);
    compare<k::gpu::RGBA8UI, k::cpu::BGRA8>(bytes, 1);
    compare<k::gpu::R8, k::cpu::BGRA8>(bytes, 1);
    k::cpu::RGBA8Snorm signedNormalized;
    unsigned char minimum[]{128, 128, 128, 128};
    co::detail::decodeImagePixels<k::gpu::RGBA8Snorm>(&signedNormalized, minimum, 1);
    require(signedNormalized.data[0] == -127);
}
} // namespace pixel_download_test
