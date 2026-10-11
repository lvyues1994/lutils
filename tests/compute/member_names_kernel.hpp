#pragma once
#include <lutils/compute/Shader.hpp>
namespace member_names {
using namespace lutils::compute::kernel;
struct Inner {
    float sample;
    std::array<uint, 2> input;
};
struct Value {
    Inner output;
    vec3 filter;
    uint m0;
};
struct LUTILS_KERNEL Names {
    static constexpr char fileLocation[] = "member_names";
    uvec3 local_size{4, 1, 1};
    BufferBinding<Value, 0> source;
    BufferBinding<Value, 1> destination;
    Uniform<Inner, 0> settings;
    float added(Inner value) { return value.sample + static_cast<Inner>(settings).sample; }
    void main() {
        auto i = gl_GlobalInvocationID.x;
        auto value = source[i];
        value.output.sample = added(value.output);
        value.output.input[1] += 3u;
        value.filter.x += value.output.sample;
        value.m0 += 7u;
        destination[i] = value;
    }
};
} // namespace member_names
