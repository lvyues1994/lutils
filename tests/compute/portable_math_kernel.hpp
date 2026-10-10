#pragma once
#include <cmath>
#include <lutils/compute/Shader.hpp>

#if LUTILS_EXPECT_ANDROID
#ifndef __ANDROID__
#error kernelc must parse Android builtins when compiling for Android
#endif
static_assert(__ANDROID_API__ == LUTILS_EXPECT_ANDROID);
#elif defined(__ANDROID__)
#error native kernelc profile unexpectedly uses Android builtins
#endif

namespace portable_math {
using namespace lutils::compute::kernel;
namespace user {
inline float sqrt(float x) { return x + 9.0f; }
} // namespace user
struct LUTILS_KERNEL Math {
    static constexpr char fileLocation[] = "portable_math";
    uvec3 local_size{8, 1, 1};
    BufferBinding<float, 0> input;
    BufferBinding<float, 1> output;
    void main() {
        auto i = gl_GlobalInvocationID.x;
        auto x = input[i];
        output[i] = std::sqrt(x) + sqrt(x) + user::sqrt(x) + std::abs(-x) + std::pow(x, 2.0f) +
                    std::floor(x) + std::ceil(x) + std::sin(x) + std::cos(x);
    }
};
struct LUTILS_KERNEL IntegerSqrt {
    static constexpr char fileLocation[] = "integer_sqrt";
    uvec3 local_size{1, 1, 1};
    BufferBinding<double, 0> output;
    void main() { output[0] = std::sqrt(2); }
};
} // namespace portable_math
