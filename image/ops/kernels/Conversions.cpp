#ifndef LUTILS_IMAGE_CONVERSIONS_KERNEL_SOURCE
#define LUTILS_IMAGE_CONVERSIONS_KERNEL_SOURCE
#include <lutils/compute/Shader.hpp>
#ifndef LUTILS_IMAGE_LOCAL_SIZE_X
#define LUTILS_IMAGE_LOCAL_SIZE_X 32
#endif

namespace lutils::image::kernels {
namespace k = lutils::compute::kernel;
struct Packed422Params {
    k::uint width;
    k::uint height;
    k::uint sourceStride;
    k::uint yStride;
    k::uint uvOffset;
    k::uint uvStride;
    k::uint yShift;
    k::uint uShift;
    k::uint vShift;
};
struct LUTILS_KERNEL Packed422ToNv12 {
    static constexpr char fileLocation[] = "packed422_to_nv12";
    k::uvec3 local_size{LUTILS_IMAGE_LOCAL_SIZE_X, 1, 1};
    k::BufferBinding<k::uint, 0> source;
    k::BufferBinding<k::uint, 1> destination;
    k::Uniform<Packed422Params, 0> parameters;
    void main() {
        auto id = k::gl_GlobalInvocationID;
        auto p = static_cast<Packed422Params>(parameters);

        if (id.x >= p.width / 4u + (p.width % 4u != 0u ? 1u : 0u))
            return;
        auto x = id.x * 4u;
        auto value = 0u;
        if (id.y < p.height) {
            for (k::uint i = 0u; i < 4u; ++i) {
                if (x + i < p.width) {
                    auto packed = source[id.y * p.sourceStride + (x + i) / 2u];
                    auto y = (packed >> (p.yShift + ((x + i) % 2u) * 16u)) & 255u;
                    value |= y << (i * 8u);
                }
            }
            destination[id.y * p.yStride + id.x] = value;
        } else {
            auto row = id.y - p.height;
            if (row >= (p.height + 1u) / 2u)
                return;
            auto top = row * 2u;
            auto bottom = top + 1u < p.height ? top + 1u : top;
            for (k::uint i = 0u; i < 2u; ++i) {
                if (x + i * 2u < p.width) {
                    auto a = source[top * p.sourceStride + x / 2u + i];
                    auto b = source[bottom * p.sourceStride + x / 2u + i];
                    auto u = (((a >> p.uShift) & 255u) + ((b >> p.uShift) & 255u) + 1u) / 2u;
                    auto v = (((a >> p.vShift) & 255u) + ((b >> p.vShift) & 255u) + 1u) / 2u;
                    value |= (u | (v << 8u)) << (i * 16u);
                }
            }
            destination[p.uvOffset + row * p.uvStride + id.x] = value;
        }
    }
};
struct RgbParams {
    k::uint width;
    k::uint height;
    k::uint yStride;
    k::uint uvOffset;
    k::uint uvStride;
    k::uint destinationStride;
};
inline k::uint quantize(std::int32_t value) {
    if (value <= 0)
        return 0u;
    if (value >= 255 * 256)
        return 255u;
    return static_cast<k::uint>((value + 128) / 256);
}
struct LUTILS_KERNEL Nv12ToRgba {
    static constexpr char fileLocation[] = "nv12_to_rgba";
    k::uvec3 local_size{LUTILS_IMAGE_LOCAL_SIZE_X, 1, 1};
    k::BufferBinding<k::uint, 0> source;
    k::BufferBinding<k::uint, 1> destination;
    k::Uniform<RgbParams, 0> parameters;
    void main() {
        auto id = k::gl_GlobalInvocationID;
        auto p = static_cast<RgbParams>(parameters);

        if (id.x >= p.width || id.y >= p.height)
            return;
        auto yWord = source[id.y * p.yStride + id.x / 4u];
        auto y = static_cast<std::int32_t>((yWord >> ((id.x % 4u) * 8u)) & 255u) - 16;
        auto uvWord = source[p.uvOffset + (id.y / 2u) * p.uvStride + id.x / 4u];
        auto shift = ((id.x / 2u) % 2u) * 16u;
        auto u = static_cast<std::int32_t>((uvWord >> shift) & 255u) - 128;
        auto v = static_cast<std::int32_t>((uvWord >> (shift + 8u)) & 255u) - 128;
        auto r = quantize(298 * y + 409 * v);
        auto g = quantize(298 * y - 100 * u - 208 * v);
        auto b = quantize(298 * y + 516 * u);
        destination[id.y * p.destinationStride + id.x] = r | (g << 8u) | (b << 16u) | 0xff000000u;
    }
};
} // namespace lutils::image::kernels
#endif
