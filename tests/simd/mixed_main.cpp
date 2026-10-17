#include "Mixed.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <lutils/simd/Dispatch.hpp>
int main() {
    if (!lutils::simd::supported(lutils::simd::Target::Sse2))
        return 77;
    std::array<std::uint8_t, 67> a{}, b{};
    b[65] = 1;
    auto baseline = baselineAddress();
    if (baseline(a.data(), b.data(), a.size()) != 65)
        return 1;
    auto const epsilon = std::ldexp(1.0f, -13);
    std::array<float, 4> x{}, y{}, z{}, out{};
    x.fill(1.0f + epsilon);
    y.fill(1.0f - epsilon);
    z.fill(-1);
    auto arithmetic = [&](auto function) {
        function(out.data(), x.data(), y.data(), z.data());
        for (float value : out)
            if (value != 0)
                return false;
        return true;
    };
    if (!arithmetic(baselineArithmetic))
        return 1;
    auto packing = [](auto function) {
        std::array<std::uint8_t, 16> in{}, out{};
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = static_cast<std::uint8_t>(i * 17);
        function(out.data(), in.data());
        for (std::size_t i = 0; i < in.size(); ++i)
            if (out[i] != in[i] / 2)
                return false;
        return true;
    };
    if (!packing(baselinePacking))
        return 1;
    auto features = lutils::simd::cpu_features();
    if (features.avx2 && features.fma) {
        auto advanced = advancedAddress();
        if (baseline == advanced || advanced(a.data(), b.data(), a.size()) != 65)
            return 1;
        if (!arithmetic(advancedArithmetic))
            return 1;
        if (!packing(advancedPacking))
            return 1;
    }
    std::cout << "mixed ISA linkage and nonfused arithmetic passed\n";
}
