#include <lutils/simd/detail/Cpu.hpp>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif

namespace lutils::simd {
namespace {
CpuFeatures detect() noexcept {
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    detail::X86Features bits;
    unsigned a = 0, b = 0, c = 0, d = 0;
    auto const maximum = __get_cpuid_max(0, nullptr);
    if (maximum >= 1) {
        __cpuid_count(1, 0, a, b, c, d);
        bits.leaf1Ecx = c;
        bits.leaf1Edx = d;
        if ((c & (1u << 26)) && (c & (1u << 27))) {
            unsigned low = 0, high = 0;
            __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
            bits.xcr0 = low | (std::uint64_t{high} << 32);
        }
    }
    if (maximum >= 7) {
        __cpuid_count(7, 0, a, b, c, d);
        bits.leaf7Ebx = b;
    }
    return detail::decode(bits);
#elif defined(__aarch64__)
    return {false, false, true, true};
#else
    return {};
#endif
}
} // namespace
CpuFeatures cpu_features() noexcept {
    static CpuFeatures const features = detect();
    return features;
}
bool supported(Target target) noexcept {
    auto const features = cpu_features();
    switch (target) {
    case Target::Automatic:
    case Target::Scalar:
        return true;
    case Target::Sse2:
        return features.sse2;
    case Target::Avx2:
        return features.avx2;
    case Target::Neon:
        return features.neon;
    }
    return false;
}
char const *target_name(Target target) noexcept {
    switch (target) {
    case Target::Automatic:
        return "automatic";
    case Target::Scalar:
        return "scalar";
    case Target::Sse2:
        return "sse2";
    case Target::Avx2:
        return "avx2";
    case Target::Neon:
        return "neon";
    }
    return "unknown";
}
} // namespace lutils::simd
