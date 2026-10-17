#pragma once
#include <lutils/simd/detail/Scalar.hpp>
namespace lutils::simd::detail {
template <class T, class A> struct ArithmeticShift {
    static constexpr bool available = false;
};
} // namespace lutils::simd::detail
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
namespace lutils::simd::detail {
#define LUTILS_SHIFT_SSE(TYPE, BITS)                                                               \
    template <> struct ArithmeticShift<TYPE, abi::sse2> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TYPE *out, TYPE const *in, unsigned n) noexcept {       \
            auto x = _mm_loadu_si128(reinterpret_cast<__m128i const *>(in));                       \
            auto count = _mm_cvtsi32_si128(static_cast<int>(n > BITS ? BITS : n));                 \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(out), _mm_sra_epi##BITS(x, count));       \
        }                                                                                          \
    };
LUTILS_SHIFT_SSE(std::int16_t, 16)
LUTILS_SHIFT_SSE(std::int32_t, 32)
#undef LUTILS_SHIFT_SSE
} // namespace lutils::simd::detail
#endif
#if defined(__AVX2__)
#include <immintrin.h>
namespace lutils::simd::detail {
#define LUTILS_SHIFT_AVX(TYPE, BITS)                                                               \
    template <> struct ArithmeticShift<TYPE, abi::avx2> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TYPE *out, TYPE const *in, unsigned n) noexcept {       \
            auto x = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(in));                    \
            auto count = _mm_cvtsi32_si128(static_cast<int>(n > BITS ? BITS : n));                 \
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), _mm256_sra_epi##BITS(x, count)); \
        }                                                                                          \
    };
LUTILS_SHIFT_AVX(std::int16_t, 16)
LUTILS_SHIFT_AVX(std::int32_t, 32)
#undef LUTILS_SHIFT_AVX
} // namespace lutils::simd::detail
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
namespace lutils::simd::detail {
#define LUTILS_SHIFT_NEON(TYPE, BITS)                                                              \
    template <> struct ArithmeticShift<TYPE, abi::neon> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TYPE *out, TYPE const *in, unsigned n) noexcept {       \
            auto count =                                                                           \
                vdupq_n_s##BITS(static_cast<TYPE>(-static_cast<int>(n > BITS ? BITS : n)));        \
            vst1q_s##BITS(out, vshlq_s##BITS(vld1q_s##BITS(in), count));                           \
        }                                                                                          \
    };
LUTILS_SHIFT_NEON(std::int8_t, 8)
LUTILS_SHIFT_NEON(std::int16_t, 16)
LUTILS_SHIFT_NEON(std::int32_t, 32)
LUTILS_SHIFT_NEON(std::int64_t, 64)
#undef LUTILS_SHIFT_NEON
} // namespace lutils::simd::detail
#endif
