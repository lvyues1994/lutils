#pragma once
#include <cstdint>
#include <lutils/simd/Dispatch.hpp>

namespace lutils::simd::detail {
struct X86Features {
    std::uint32_t leaf1Ecx = 0;
    std::uint32_t leaf1Edx = 0;
    std::uint32_t leaf7Ebx = 0;
    std::uint64_t xcr0 = 0;
};
constexpr CpuFeatures decode(X86Features bits) noexcept {
    constexpr auto avxStateBits = (1u << 26) | (1u << 27) | (1u << 28);
    bool const avx = (bits.leaf1Ecx & avxStateBits) == avxStateBits && (bits.xcr0 & 6) == 6;
    return {(bits.leaf1Edx & (1u << 26)) != 0, avx && (bits.leaf7Ebx & (1u << 5)) != 0, false,
            avx && (bits.leaf1Ecx & (1u << 12)) != 0};
}
} // namespace lutils::simd::detail
