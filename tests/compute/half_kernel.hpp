#pragma once
#include <lutils/compute/Shader.hpp>
namespace half_test {
using namespace lutils::compute::kernel;
struct Mixed {
    half a;
    f16vec3 b;
    half c;
    float d;
};
struct Nested {
    Mixed value;
    std::array<half, 3> tail;
};
struct LUTILS_KERNEL Arithmetic {
    static constexpr char fileLocation[] = "half_arithmetic";
    uvec3 local_size{4, 1, 1};
    BufferBinding<half, 0> input;
    BufferBinding<half, 1> output;
    BufferBinding<Nested, 2> records;
    Uniform<half, 0> scale;
    Uniform<Mixed, 1> params;
    void main() {
        uint i = gl_GlobalInvocationID.x;
        half factor = scale;
        half value = input[i] * factor;
        output[i] = value + half(0.5f);
        Mixed p = params;
        records[i].value.a = p.a + input[i];
        records[i].value.b = p.b * f16vec3(half(2.0f));
        records[i].value.c = p.c;
        records[i].value.d = float(input[i]);
        records[i].tail[0] = sqrt(half(4.0f));
        records[i].tail[1] = min(half(2.0f), half(3.0f));
        records[i].tail[2] = dot(f16vec2(half(1.0f)), f16vec2(half(2.0f)));
    }
};
struct LUTILS_KERNEL Shared {
    static constexpr char fileLocation[] = "half_shared";
    uvec3 local_size{4, 1, 1};
    BufferBinding<half, 0> output;
    SharedArray<half, 4> values;
    void main() {
        uint i = gl_LocalInvocationIndex;
        values[i] = half(float(i));
        barrier();
        output[gl_GlobalInvocationID.x] = values[3u - i];
    }
};
} // namespace half_test
