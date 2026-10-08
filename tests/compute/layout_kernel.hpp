#pragma once
#include <lutils/compute/Shader.hpp>
namespace layout_test {
using namespace lutils::compute::kernel;
struct Inner {
    vec3 axis;
    float gain;
};
struct Payload {
    int tag;
    Inner inner;
    std::array<vec2, 2> offsets;
    double weight;
};
struct LUTILS_KERNEL Layout {
    static constexpr char fileLocation[] = "layout";
    uvec3 local_size{4, 1, 1};
    BufferBinding<Payload, 3> input;
    BufferBinding<Payload, 7> output;
    BufferBinding<vec3, 9> vectors;
    Uniform<bool, 2> enabled{true};
    Uniform<float, 3> factor{2.0f};
    void main() {
        uint i = gl_GlobalInvocationID.x;
        Payload p = input[i];
        if (enabled) {
            p.inner.gain += float(factor);
            p.offsets[1] += vec2{3.0f, 4.0f};
            p.weight += 0.5;
        }
        p.inner.axis.x = float(p.inner.axis["x"_sw]);
        output[i] = p;
        vectors[i] = vec3{p.inner.axis["zyx"_sw]};
    }
};
} // namespace layout_test
