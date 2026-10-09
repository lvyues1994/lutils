#pragma once
#include <Factor.hpp>
#include <lutils/compute/Shader.hpp>
namespace consumer {
using namespace lutils::compute::kernel;
struct LUTILS_KERNEL Add {
    static constexpr char fileLocation[] = "consumer_shader";
    uvec3 local_size{4, 1, 1};
    BufferBinding<float, 0> output;
    void main() { output[gl_GlobalInvocationID.x] = float(gl_GlobalInvocationID.x) + FACTOR; }
};
} // namespace consumer
