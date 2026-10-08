#pragma once
#include <lutils/compute/Shader.hpp>
namespace reject_test {
using namespace lutils::compute::kernel;
struct V {
    float x;
};
inline bool operator==(V, V) { return false; }
struct Empty {
    std::array<int, 0> a;
    int x;
};
struct LUTILS_KERNEL run {
    static constexpr char fileLocation[] = "reject";
    uvec3 local_size{1, 1, 1};
    BufferBinding<int, 0> out;
#if CASE == 1
    void main() {
        V a{1.0f};
        V b{1.0f};
        out[0] = (a == b) ? 1 : 2;
    }
#elif CASE == 2
    const std::array<int, 1> values{{1}};
    run() : values{{9}} {}
    void main() { out[0] = values[0]; }
#elif CASE == 3
    std::array<int, 1> make() {
        out[1] = 9;
        return {7};
    }
    void main() { out[0] = int(make().size()); }
#elif CASE == 4
    BufferBinding<Empty, 1> input;
    void main() { out[0] = input[0].x; }
#elif CASE == 5
    void main() {
        auto p = new int(3);
        out[0] = *p;
        delete p;
    }
#elif CASE == 6
    Uniform<int, 0> value;
    void main() {
        value = 9;
        out[0] = int(value);
    }
#elif CASE == 7
    void main() {
        gl_GlobalInvocationID.x = 0;
        out[0] = 1;
    }
#elif CASE == 8
    std::array<int, 1> values{{1}};
    void main() {
        values[0] = 2;
        out[0] = values[0];
    }
#elif CASE == 9
    int recursive(int n) { return n ? recursive(n - 1) : 1; }
    void main() { out[0] = recursive(2); }
#elif CASE == 10
    BufferBinding<int, 0> other;
    void main() { out[0] = other[0]; }
#elif CASE == 11
    void main() { out[0] = (0xffffffffULL + 1ULL == 0ULL) ? 1 : 2; }
#elif CASE == 12
    void main() {
        int i = 0;
        out[i++] = i;
    }
#elif CASE == 13
    Uniform<int, 0> value{3};
    int get() { return value; }
    void main() { out[0] = run{uvec3{1, 1, 1}, {}, Uniform<int, 0>{7}}.get(); }
#elif CASE == 14
    void main() { out[0] = int(reinterpret_cast<float &>(out[1])); }
#elif CASE == 15
    void main() {
        vec2 v{vec4{1.0f}["xyz"_sw]};
        out[0] = int(v.x);
    }
#endif
};
} // namespace reject_test
