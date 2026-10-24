#pragma once
#include <lutils/simd/detail/Avx2.hpp>
#include <lutils/simd/detail/Fixed.hpp>
#include <lutils/simd/detail/Neon.hpp>
#include <lutils/simd/detail/Sse2.hpp>

namespace lutils::simd::detail {
template <class T, class A> struct MaskOps {
    using U = MaskLane<T>;
    static LUTILS_SIMD_INLINE std::uint64_t bits(U const *p) noexcept {
#if defined(__AVX2__)
        if constexpr (std::is_same_v<A, abi::avx2>)
            return avxMask<U>(_mm256_loadu_si256(reinterpret_cast<__m256i const *>(p)));
#endif
#if defined(__SSE2__)
        if constexpr (std::is_same_v<A, abi::sse2>) {
            if constexpr (std::is_integral_v<T> && sizeof(T) == 8)
                // SSE2 compares 64-bit integer lanes scalarly. Do not pack the
                // booleans into an XMM register only to extract them again.
                return std::uint64_t(p[0] != 0) | (std::uint64_t(p[1] != 0) << 1);
            else
                return sseMask<U>(_mm_loadu_si128(reinterpret_cast<__m128i const *>(p)));
        }
#endif
#if defined(__aarch64__)
        if constexpr (std::is_same_v<A, abi::neon>) {
            if constexpr (sizeof(U) == 1)
                return neonMask<U>(vld1q_u8(p));
            else if constexpr (sizeof(U) == 2)
                return neonMask<U>(vld1q_u16(p));
            else if constexpr (sizeof(U) == 4)
                return neonMask<U>(vld1q_u32(p));
            else
                return neonMask<U>(vld1q_u64(p));
        }
#endif
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < lanes<T, A>; ++i)
            result |= std::uint64_t(p[i] != 0) << i;
        return result;
    }
};
template <class T, std::size_t N, class A> struct MaskOps<T, abi::fixed<N, A>> {
    using U = MaskLane<T>;
    static LUTILS_SIMD_INLINE std::uint64_t bits(U const *p) noexcept {
        constexpr auto width = lanes<T, A>;
        std::uint64_t result = 0;
        std::size_t i = 0;
        for (; N - i >= width; i += width)
            result |= MaskOps<T, A>::bits(p + i) << i;
        for (; i < N; ++i)
            result |= std::uint64_t(p[i] != 0) << i;
        return result;
    }
};
} // namespace lutils::simd::detail
