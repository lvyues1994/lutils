#pragma once
#include <lutils/compute/Shader.hpp>
namespace sampler_test {
namespace k = lutils::compute::kernel;
struct LUTILS_KERNEL Nearest {
    static constexpr char fileLocation[] = "nearest_sample";
    k::uvec3 local_size{4, 1, 1};
    k::SamplerBinding<3> image;
    k::BufferBinding<k::vec2, 0> positions;
    k::BufferBinding<k::vec4, 1> output;
    void main() {
        auto i = k::gl_GlobalInvocationID.x;
        output[i] = k::sampleNearest(image, positions[i]);
    }
};
} // namespace sampler_test
