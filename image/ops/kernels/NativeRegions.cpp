#include <lutils/compute/Shader.hpp>

namespace lutils::image::kernels {
namespace k = lutils::compute::kernel;
struct NativeRegionParams {
    k::uint firstWord, wordCount, gridWidth;
    k::uint sourceX, sourceY, sourceWidth, sourceHeight;
    k::uint pixelBytes, subsample, channel;
    k::uint destinationOffset, destinationStride, destinationX, destinationY;
    k::uint copyBytes, copyRows, rowBytes, rows;
    k::uint background, patternBytes;
};
struct LUTILS_KERNEL NativeRegion {
    static constexpr char fileLocation[] = "image_native_region";
    k::uvec3 local_size{64, 1, 1};
    k::SamplerBinding<0> source;
    k::BufferBinding<k::uint, 1> destination;
    k::Uniform<NativeRegionParams, 0> parameters;
    void main() {
        auto id = k::gl_GlobalInvocationID;
        auto p = static_cast<NativeRegionParams>(parameters);
        auto index = id.y * p.gridWidth + id.x;
        if (id.x >= p.gridWidth || index >= p.wordCount)
            return;
        auto word = p.firstWord + index;
        auto value = static_cast<k::uint>(destination[word]);
        auto changed = false;
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
                        auto sx = p.sourceX + (x - p.destinationX) / p.pixelBytes * p.subsample;
                        auto sy = p.sourceY + (y - p.destinationY) * p.subsample;
                        auto uv = k::vec2{
                            (static_cast<float>(sx) + 0.5f) / static_cast<float>(p.sourceWidth),
                            (static_cast<float>(sy) + 0.5f) / static_cast<float>(p.sourceHeight)};
                        auto sample = k::sampleNearest(source, uv);
                        auto channel = p.pixelBytes == 4u ? (x - p.destinationX) % 4u : p.channel;
                        byte = static_cast<k::uint>(k::clamp(sample[channel], 0.0f, 1.0f) * 255.0f +
                                                    0.5f);
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
