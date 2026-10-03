#ifndef LUTILS_IMAGE_CONVERSIONS_KERNEL_SOURCE
#define LUTILS_IMAGE_CONVERSIONS_KERNEL_SOURCE
#include <lutils/compute/Kernel.hpp>

namespace lutils::image::kernels {
namespace k = lutils::compute::kernel;
struct Packed422Params {
    k::U32 width;
    k::U32 height;
    k::U32 sourceStride;
    k::U32 yStride;
    k::U32 uvOffset;
    k::U32 uvStride;
    k::U32 yShift;
    k::U32 uShift;
    k::U32 vShift;
};
inline void packed422ToNv12(k::Invocation id, k::ReadBuffer source, k::WriteBuffer destination,
                            Packed422Params p) {
    if (id.x >= p.width / 4u + (p.width % 4u != 0u ? 1u : 0u))
        return;
    auto x = id.x * 4u;
    auto value = 0u;
    if (id.y < p.height) {
        for (k::U32 i = 0u; i < 4u; ++i) {
            if (x + i < p.width) {
                auto packed = source.load(id.y * p.sourceStride + (x + i) / 2u);
                auto y = (packed >> (p.yShift + ((x + i) % 2u) * 16u)) & 255u;
                value |= y << (i * 8u);
            }
        }
        destination.store(id.y * p.yStride + id.x, value);
    } else {
        auto row = id.y - p.height;
        if (row >= (p.height + 1u) / 2u)
            return;
        auto top = row * 2u;
        auto bottom = top + 1u < p.height ? top + 1u : top;
        for (k::U32 i = 0u; i < 2u; ++i) {
            if (x + i * 2u < p.width) {
                auto a = source.load(top * p.sourceStride + x / 2u + i);
                auto b = source.load(bottom * p.sourceStride + x / 2u + i);
                auto u = (((a >> p.uShift) & 255u) + ((b >> p.uShift) & 255u) + 1u) / 2u;
                auto v = (((a >> p.vShift) & 255u) + ((b >> p.vShift) & 255u) + 1u) / 2u;
                value |= (u | (v << 8u)) << (i * 16u);
            }
        }
        destination.store(p.uvOffset + row * p.uvStride + id.x, value);
    }
}
struct RgbParams {
    k::U32 width;
    k::U32 height;
    k::U32 yStride;
    k::U32 uvOffset;
    k::U32 uvStride;
    k::U32 destinationStride;
};
inline k::U32 quantize(k::I32 value) {
    if (value <= 0)
        return 0u;
    if (value >= 255 * 256)
        return 255u;
    return static_cast<k::U32>((value + 128) / 256);
}
inline void nv12ToRgba(k::Invocation id, k::ReadBuffer source, k::WriteBuffer destination,
                       RgbParams p) {
    if (id.x >= p.width || id.y >= p.height)
        return;
    auto yWord = source.load(id.y * p.yStride + id.x / 4u);
    auto y = static_cast<k::I32>((yWord >> ((id.x % 4u) * 8u)) & 255u) - 16;
    auto uvWord = source.load(p.uvOffset + (id.y / 2u) * p.uvStride + id.x / 4u);
    auto shift = ((id.x / 2u) % 2u) * 16u;
    auto u = static_cast<k::I32>((uvWord >> shift) & 255u) - 128;
    auto v = static_cast<k::I32>((uvWord >> (shift + 8u)) & 255u) - 128;
    auto r = quantize(298 * y + 409 * v);
    auto g = quantize(298 * y - 100 * u - 208 * v);
    auto b = quantize(298 * y + 516 * u);
    destination.store(id.y * p.destinationStride + id.x, r | (g << 8u) | (b << 16u) | 0xff000000u);
}
} // namespace lutils::image::kernels
#endif
