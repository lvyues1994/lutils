#pragma once
#include "Rows.hpp"
#include <lutils/simd.hpp>

namespace lutils::image::simd_detail {
template <class Abi>
void convertRows(std::uint8_t const *top, std::uint8_t const *bottom, std::uint8_t *y0,
                 std::uint8_t *y1, std::uint8_t *uv, std::size_t width) {
    using V = simd::Batch<std::uint8_t, Abi>;
    std::size_t x = 0;
    if constexpr (std::is_same_v<Abi, simd::abi::avx2>) {
        // Align all three output streams when their row offsets permit it.
        auto y0Offset = reinterpret_cast<std::uintptr_t>(y0) % V::size;
        auto y1Offset = reinterpret_cast<std::uintptr_t>(y1) % V::size;
        auto uvOffset = reinterpret_cast<std::uintptr_t>(uv) % V::size;
        auto prefix = (V::size - y0Offset) % V::size;
        if (width >= 2 * V::size && y0Offset == y1Offset && y0Offset == uvOffset)
            for (; x < prefix; ++x) {
                y0[x] = top[2 * x + 1];
                y1[x] = bottom[2 * x + 1];
                uv[x] = static_cast<std::uint8_t>(
                    (unsigned(top[2 * x]) + unsigned(bottom[2 * x]) + 1u) / 2u);
            }
    }
    while (width - x >= V::size) {
        auto a =
            simd::deinterleave(simd::load<V>(top + 2 * x), simd::load<V>(top + 2 * x + V::size));
        auto b = simd::deinterleave(simd::load<V>(bottom + 2 * x),
                                    simd::load<V>(bottom + 2 * x + V::size));
        simd::store(y0 + x, a.odd);
        simd::store(y1 + x, b.odd);
        simd::store(uv + x, simd::average_round_up(a.even, b.even));
        x += V::size;
    }
    // Explicit scalar cleanup avoids loading a second vector past a short row.
    for (; x < width; ++x) {
        y0[x] = top[2 * x + 1];
        y1[x] = bottom[2 * x + 1];
        uv[x] =
            static_cast<std::uint8_t>((unsigned(top[2 * x]) + unsigned(bottom[2 * x]) + 1u) / 2u);
    }
}
} // namespace lutils::image::simd_detail

namespace lutils::image::simd_detail {
inline void rgbaPixel(std::uint8_t y, std::uint8_t u, std::uint8_t v, std::uint8_t *out) {
    auto quantize = [](int value) {
        return static_cast<std::uint8_t>(value <= 0           ? 0
                                         : value >= 255 * 256 ? 255
                                                              : (value + 128) / 256);
    };
    int c = int(y) - 16, d = int(u) - 128, e = int(v) - 128;
    out[0] = quantize(298 * c + 409 * e);
    out[1] = quantize(298 * c - 100 * d - 208 * e);
    out[2] = quantize(298 * c + 516 * d);
    out[3] = 255;
}
template <class A> auto channels(simd::Batch<std::uint8_t, A> value, std::int16_t offset) {
    using I = simd::Batch<std::int32_t, A>;
    simd::WideParts<I, 4> result{};
    auto halves = simd::widen<std::uint16_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        auto signedHalf =
            simd::bit_cast<std::int16_t>(halves.parts[i]) - simd::Batch<std::int16_t, A>{offset};
        auto quarters = simd::widen<std::int32_t>(signedHalf);
        result.parts[2 * i] = quarters.parts[0];
        result.parts[2 * i + 1] = quarters.parts[1];
    }
    return result;
}
template <class A> auto packChannel(simd::WideParts<simd::Batch<std::int32_t, A>, 4> const &value) {
    using I = simd::Batch<std::int32_t, A>;
    using H = simd::Batch<std::int16_t, A>;
    simd::WideParts<H, 2> halves{};
    for (std::size_t i = 0; i < 2; ++i)
        halves.parts[i] = simd::narrow_saturate<std::int16_t>(
            simd::WideParts<I, 2>{{value.parts[2 * i], value.parts[2 * i + 1]}});
    return simd::narrow_saturate<std::uint8_t>(halves);
}
template <class A>
void rgbaRow(std::uint8_t const *y, std::uint8_t const *uv, std::uint8_t *out, std::size_t width) {
    using B = simd::Batch<std::uint8_t, A>;
    using I = simd::Batch<std::int32_t, A>;
    std::size_t x = 0;
    if constexpr (B::size > 1) {
        for (; width - x >= B::size; x += B::size) {
            auto chroma = simd::load<B>(uv + x);
            auto split = simd::deinterleave(chroma, chroma);
            auto c = channels(simd::load<B>(y + x), 16);
            auto d = channels(simd::interleave(split.even, split.even).low, 128);
            auto e = channels(simd::interleave(split.odd, split.odd).low, 128);
            simd::WideParts<I, 4> r{}, g{}, b{};
            for (std::size_t i = 0; i < 4; ++i) {
                auto luma = c.parts[i] * I{298};
                r.parts[i] = simd::shift_right_arithmetic(luma + e.parts[i] * I{409} + I{128}, 8);
                g.parts[i] = simd::shift_right_arithmetic(
                    luma - d.parts[i] * I{100} - e.parts[i] * I{208} + I{128}, 8);
                b.parts[i] = simd::shift_right_arithmetic(luma + d.parts[i] * I{516} + I{128}, 8);
            }
            auto rg = simd::interleave(packChannel(r), packChannel(g));
            auto ba = simd::interleave(packChannel(b), B{255});
            auto low = simd::interleave(simd::bit_cast<std::uint16_t>(rg.low),
                                        simd::bit_cast<std::uint16_t>(ba.low));
            auto high = simd::interleave(simd::bit_cast<std::uint16_t>(rg.high),
                                         simd::bit_cast<std::uint16_t>(ba.high));
            simd::store(out + 4 * x, simd::bit_cast<std::uint8_t>(low.low));
            simd::store(out + 4 * x + B::size, simd::bit_cast<std::uint8_t>(low.high));
            simd::store(out + 4 * x + 2 * B::size, simd::bit_cast<std::uint8_t>(high.low));
            simd::store(out + 4 * x + 3 * B::size, simd::bit_cast<std::uint8_t>(high.high));
        }
    }
    for (; x < width; ++x)
        rgbaPixel(y[x], uv[2 * (x / 2)], uv[2 * (x / 2) + 1], out + 4 * x);
}
template <class A> void uyvyFrame(ConstFrameView const &source, FrameView const &destination) {
    for (std::size_t y = 0; y < source.desc.height; y += 2) {
        auto bottom = y + 1 < source.desc.height ? y + 1 : y;
        convertRows<A>(reinterpret_cast<std::uint8_t const *>(row(source.planes[0], y)),
                       reinterpret_cast<std::uint8_t const *>(row(source.planes[0], bottom)),
                       reinterpret_cast<std::uint8_t *>(row(destination.planes[0], y)),
                       reinterpret_cast<std::uint8_t *>(row(destination.planes[0], bottom)),
                       reinterpret_cast<std::uint8_t *>(row(destination.planes[1], y / 2)),
                       source.desc.width);
    }
}
template <class A> void rgbaFrame(ConstFrameView const &source, FrameView const &destination) {
    for (std::size_t y = 0; y < source.desc.height; ++y)
        rgbaRow<A>(reinterpret_cast<std::uint8_t const *>(row(source.planes[0], y)),
                   reinterpret_cast<std::uint8_t const *>(row(source.planes[1], y / 2)),
                   reinterpret_cast<std::uint8_t *>(row(destination.planes[0], y)),
                   source.desc.width);
}
} // namespace lutils::image::simd_detail
