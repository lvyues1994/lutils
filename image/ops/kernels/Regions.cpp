#include <lutils/compute/Shader.hpp>

namespace lutils::image::kernels {
namespace k = lutils::compute::kernel;
struct RegionParams {
    k::uint firstWord, wordCount, gridWidth;
    k::uint sourceOffset, sourceStride, sourceX, sourceY;
    k::uint destinationOffset, destinationStride, destinationX, destinationY;
    k::uint copyBytes, copyRows, rowBytes, rows;
    k::uint background, patternBytes;
};
struct LUTILS_KERNEL RegionBytes {
    static constexpr char fileLocation[] = "image_region_bytes";
    k::uvec3 local_size{64, 1, 1};
    k::BufferBinding<k::uint, 0> source;
    k::BufferBinding<k::uint, 1> destination;
    k::Uniform<RegionParams, 0> parameters;
    void main() {
        auto id = k::gl_GlobalInvocationID;
        auto p = static_cast<RegionParams>(parameters);
        if (id.x >= p.gridWidth)
            return;
        auto index = id.y * p.gridWidth + id.x;
        if (index >= p.wordCount)
            return;
        auto word = p.firstWord + index;
        auto value = static_cast<k::uint>(destination[word]);
        auto changed = false;
        // One invocation owns an absolute word, even when a word spans two rows.
        for (k::uint lane = 0u; lane < 4u; ++lane) {
            auto address = word * 4u + lane;
            if (address >= p.destinationOffset) {
                auto relative = address - p.destinationOffset;
                auto y = relative / p.destinationStride;
                auto x = relative % p.destinationStride;
                if (y < p.rows && x < p.rowBytes) {
                    auto byte = 0u;
                    if (x >= p.destinationX && x - p.destinationX < p.copyBytes &&
                        y >= p.destinationY && y - p.destinationY < p.copyRows) {
                        auto at = p.sourceOffset +
                                  (p.sourceY + y - p.destinationY) * p.sourceStride + p.sourceX +
                                  x - p.destinationX;
                        byte = (source[at / 4u] >> ((at % 4u) * 8u)) & 255u;
                    } else {
                        byte = (p.background >> ((x % p.patternBytes) * 8u)) & 255u;
                    }
                    auto shift = lane * 8u;
                    value = (value & ~(255u << shift)) | (byte << shift);
                    changed = true;
                }
            }
        }
        if (changed)
            destination[word] = value;
    }
};
} // namespace lutils::image::kernels
