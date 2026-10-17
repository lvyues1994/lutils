#pragma once
#include <lutils/simd/detail/Scalar.hpp>
namespace lutils::simd::detail {
template <class To, class From, class A> struct WidenNative {
    static constexpr bool available = false;
};
template <class To, class From, class A> struct NarrowNative {
    static constexpr bool available = false;
};
} // namespace lutils::simd::detail
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
namespace lutils::simd::detail {
#define LUTILS_WIDEN_SSE(TO, FROM, BITS, SIGN)                                                     \
    template <> struct WidenNative<TO, FROM, abi::sse2> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TO *lo, TO *hi, FROM const *in) noexcept {              \
            auto x = _mm_loadu_si128(reinterpret_cast<__m128i const *>(in));                       \
            auto sign = SIGN;                                                                      \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(lo), _mm_unpacklo_epi##BITS(x, sign));    \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(hi), _mm_unpackhi_epi##BITS(x, sign));    \
        }                                                                                          \
    };
LUTILS_WIDEN_SSE(std::uint16_t, std::uint8_t, 8, _mm_setzero_si128())
LUTILS_WIDEN_SSE(std::int16_t, std::int8_t, 8, _mm_cmpgt_epi8(_mm_setzero_si128(), x))
LUTILS_WIDEN_SSE(std::uint32_t, std::uint16_t, 16, _mm_setzero_si128())
LUTILS_WIDEN_SSE(std::int32_t, std::int16_t, 16, _mm_srai_epi16(x, 15))
LUTILS_WIDEN_SSE(std::uint64_t, std::uint32_t, 32, _mm_setzero_si128())
LUTILS_WIDEN_SSE(std::int64_t, std::int32_t, 32, _mm_srai_epi32(x, 31))
#undef LUTILS_WIDEN_SSE
#define LUTILS_NARROW_SSE(TO, FROM, PREP, PACK)                                                    \
    template <> struct NarrowNative<TO, FROM, abi::sse2> {                                         \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE __m128i prepare(__m128i x) noexcept { return PREP; }             \
        static LUTILS_SIMD_INLINE void run(TO *out, FROM const *lo, FROM const *hi) noexcept {     \
            auto a = _mm_loadu_si128(reinterpret_cast<__m128i const *>(lo));                       \
            auto b = _mm_loadu_si128(reinterpret_cast<__m128i const *>(hi));                       \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(out), PACK(prepare(a), prepare(b)));      \
        }                                                                                          \
    };
LUTILS_NARROW_SSE(std::uint8_t, std::int16_t, x, _mm_packus_epi16)
LUTILS_NARROW_SSE(std::int8_t, std::int16_t, x, _mm_packs_epi16)
LUTILS_NARROW_SSE(std::int16_t, std::int32_t, x, _mm_packs_epi32)
LUTILS_NARROW_SSE(std::uint8_t, std::uint16_t,
                  _mm_sub_epi16(x, _mm_subs_epu16(x, _mm_set1_epi16(255))), _mm_packus_epi16)
#undef LUTILS_NARROW_SSE
} // namespace lutils::simd::detail
#endif
#if defined(__AVX2__)
#include <immintrin.h>
namespace lutils::simd::detail {
#define LUTILS_WIDEN_AVX(TO, FROM, INTRINSIC)                                                      \
    template <> struct WidenNative<TO, FROM, abi::avx2> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TO *lo, TO *hi, FROM const *in) noexcept {              \
            auto x = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(in));                    \
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(lo),                                   \
                                INTRINSIC(_mm256_castsi256_si128(x)));                             \
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(hi),                                   \
                                INTRINSIC(_mm256_extracti128_si256(x, 1)));                        \
        }                                                                                          \
    };
LUTILS_WIDEN_AVX(std::uint16_t, std::uint8_t, _mm256_cvtepu8_epi16)
LUTILS_WIDEN_AVX(std::int16_t, std::int8_t, _mm256_cvtepi8_epi16)
LUTILS_WIDEN_AVX(std::uint32_t, std::uint16_t, _mm256_cvtepu16_epi32)
LUTILS_WIDEN_AVX(std::int32_t, std::int16_t, _mm256_cvtepi16_epi32)
LUTILS_WIDEN_AVX(std::uint64_t, std::uint32_t, _mm256_cvtepu32_epi64)
LUTILS_WIDEN_AVX(std::int64_t, std::int32_t, _mm256_cvtepi32_epi64)
#undef LUTILS_WIDEN_AVX
#define LUTILS_NARROW_AVX(TO, FROM, PREP, PACK)                                                    \
    template <> struct NarrowNative<TO, FROM, abi::avx2> {                                         \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE __m256i prepare(__m256i x) noexcept { return PREP; }             \
        static LUTILS_SIMD_INLINE void run(TO *out, FROM const *lo, FROM const *hi) noexcept {     \
            auto a = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(lo));                    \
            auto b = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(hi));                    \
            auto packed = PACK(prepare(a), prepare(b));                                            \
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(out),                                  \
                                _mm256_permute4x64_epi64(packed, 0xd8));                           \
        }                                                                                          \
    };
LUTILS_NARROW_AVX(std::uint8_t, std::int16_t, x, _mm256_packus_epi16)
LUTILS_NARROW_AVX(std::int8_t, std::int16_t, x, _mm256_packs_epi16)
LUTILS_NARROW_AVX(std::int16_t, std::int32_t, x, _mm256_packs_epi32)
LUTILS_NARROW_AVX(std::uint8_t, std::uint16_t, _mm256_min_epu16(x, _mm256_set1_epi16(255)),
                  _mm256_packus_epi16)
#undef LUTILS_NARROW_AVX
} // namespace lutils::simd::detail
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
namespace lutils::simd::detail {
#define LUTILS_WIDEN_NEON(TO, FROM, OUT, IN)                                                       \
    template <> struct WidenNative<TO, FROM, abi::neon> {                                          \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TO *lo, TO *hi, FROM const *in) noexcept {              \
            auto x = vld1q_##IN(in);                                                               \
            vst1q_##OUT(lo, vmovl_##IN(vget_low_##IN(x)));                                         \
            vst1q_##OUT(hi, vmovl_high_##IN(x));                                                   \
        }                                                                                          \
    };
LUTILS_WIDEN_NEON(std::uint16_t, std::uint8_t, u16, u8)
LUTILS_WIDEN_NEON(std::int16_t, std::int8_t, s16, s8)
LUTILS_WIDEN_NEON(std::uint32_t, std::uint16_t, u32, u16)
LUTILS_WIDEN_NEON(std::int32_t, std::int16_t, s32, s16)
LUTILS_WIDEN_NEON(std::uint64_t, std::uint32_t, u64, u32)
LUTILS_WIDEN_NEON(std::int64_t, std::int32_t, s64, s32)
#undef LUTILS_WIDEN_NEON
#define LUTILS_NARROW_NEON(TO, FROM, OUT, IN, PACK)                                                \
    template <> struct NarrowNative<TO, FROM, abi::neon> {                                         \
        static constexpr bool available = true;                                                    \
        static LUTILS_SIMD_INLINE void run(TO *out, FROM const *lo, FROM const *hi) noexcept {     \
            vst1q_##OUT(out, vcombine_##OUT(PACK(vld1q_##IN(lo)), PACK(vld1q_##IN(hi))));          \
        }                                                                                          \
    };
LUTILS_NARROW_NEON(std::uint8_t, std::int16_t, u8, s16, vqmovun_s16)
LUTILS_NARROW_NEON(std::int8_t, std::int16_t, s8, s16, vqmovn_s16)
LUTILS_NARROW_NEON(std::int16_t, std::int32_t, s16, s32, vqmovn_s32)
LUTILS_NARROW_NEON(std::uint8_t, std::uint16_t, u8, u16, vqmovn_u16)
#undef LUTILS_NARROW_NEON
} // namespace lutils::simd::detail
#endif
