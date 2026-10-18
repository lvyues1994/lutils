#pragma once
#include <lutils/simd/Rounding.hpp>
#include <lutils/simd/detail/Scalar.hpp>
namespace lutils::simd::detail {
template <class To, class From, class A> struct FloatToIntegerNative {
    static constexpr bool available = false;
};
} // namespace lutils::simd::detail
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
namespace lutils::simd::detail {
template <class To> struct FloatToIntegerNative<To, float, abi::sse2> {
    static constexpr bool available =
        std::is_same_v<To, std::int32_t> || std::is_same_v<To, std::uint32_t>;
    static LUTILS_SIMD_INLINE void run(To *out, float const *in, Rounding mode) noexcept {
        auto x = _mm_loadu_ps(in);
        x = _mm_and_ps(x, _mm_cmpord_ps(x, x)); // NaN -> zero before conversion.
        auto low = _mm_cmple_ps(x, _mm_set1_ps(std::is_signed_v<To> ? -2147483648.0f : 0.0f));
        auto high =
            _mm_cmpge_ps(x, _mm_set1_ps(std::is_signed_v<To> ? 2147483648.0f : 4294967296.0f));
        auto safe = _mm_andnot_ps(_mm_or_ps(low, high), x);
        auto upper = _mm_cmpge_ps(safe, _mm_set1_ps(2147483648.0f));
        if constexpr (std::is_unsigned_v<To>)
            safe = _mm_sub_ps(safe, _mm_and_ps(upper, _mm_set1_ps(2147483648.0f)));
        auto value = _mm_cvttps_epi32(safe);
        auto truncated = _mm_cvtepi32_ps(value); // Exact: safe came from a float.
        auto one = _mm_set1_epi32(1);
        if (mode == Rounding::Downward)
            value = _mm_sub_epi32(
                value, _mm_and_si128(_mm_castps_si128(_mm_cmplt_ps(safe, truncated)), one));
        else if (mode == Rounding::Upward)
            value = _mm_add_epi32(
                value, _mm_and_si128(_mm_castps_si128(_mm_cmpgt_ps(safe, truncated)), one));
        else if (mode == Rounding::NearestEven) {
            auto fraction = _mm_andnot_ps(_mm_set1_ps(-0.0f), _mm_sub_ps(safe, truncated));
            auto odd = _mm_cmpeq_epi32(_mm_and_si128(value, one), one);
            auto adjust = _mm_or_si128(
                _mm_castps_si128(_mm_cmpgt_ps(fraction, _mm_set1_ps(0.5f))),
                _mm_and_si128(odd, _mm_castps_si128(_mm_cmpeq_ps(fraction, _mm_set1_ps(0.5f)))));
            auto direction = _mm_or_si128(_mm_srai_epi32(_mm_castps_si128(safe), 31), one);
            value = _mm_add_epi32(value, _mm_and_si128(adjust, direction));
        }
        if constexpr (std::is_unsigned_v<To>)
            value = _mm_or_si128(value,
                                 _mm_and_si128(_mm_castps_si128(upper), _mm_set1_epi32(INT32_MIN)));
        auto minimum = _mm_set1_epi32(std::is_signed_v<To> ? INT32_MIN : 0);
        auto maximum = _mm_set1_epi32(std::is_signed_v<To> ? INT32_MAX : -1);
        value = _mm_or_si128(_mm_andnot_si128(_mm_castps_si128(low), value),
                             _mm_and_si128(_mm_castps_si128(low), minimum));
        value = _mm_or_si128(_mm_andnot_si128(_mm_castps_si128(high), value),
                             _mm_and_si128(_mm_castps_si128(high), maximum));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(out), value);
    }
};
} // namespace lutils::simd::detail
#endif
#if defined(__AVX2__)
#include <immintrin.h>
namespace lutils::simd::detail {
template <class To> struct FloatToIntegerNative<To, float, abi::avx2> {
    static constexpr bool available =
        std::is_same_v<To, std::int32_t> || std::is_same_v<To, std::uint32_t>;
    static LUTILS_SIMD_INLINE void run(To *out, float const *in, Rounding mode) noexcept {
        auto x = _mm256_loadu_ps(in);
        x = _mm256_and_ps(x, _mm256_cmp_ps(x, x, _CMP_ORD_Q));
        auto low = _mm256_cmp_ps(x, _mm256_set1_ps(std::is_signed_v<To> ? -2147483648.0f : 0.0f),
                                 _CMP_LE_OQ);
        auto high = _mm256_cmp_ps(
            x, _mm256_set1_ps(std::is_signed_v<To> ? 2147483648.0f : 4294967296.0f), _CMP_GE_OQ);
        auto safe = _mm256_andnot_ps(_mm256_or_ps(low, high), x);
        switch (mode) {
        case Rounding::TowardZero:
            break;
        case Rounding::NearestEven:
            safe = _mm256_round_ps(safe, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            break;
        case Rounding::Downward:
            safe = _mm256_round_ps(safe, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
            break;
        case Rounding::Upward:
            safe = _mm256_round_ps(safe, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
            break;
        }
        auto upper = _mm256_cmp_ps(safe, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ);
        if constexpr (std::is_unsigned_v<To>)
            safe = _mm256_sub_ps(safe, _mm256_and_ps(upper, _mm256_set1_ps(2147483648.0f)));
        auto value = _mm256_cvttps_epi32(safe);
        if constexpr (std::is_unsigned_v<To>)
            value = _mm256_or_si256(
                value, _mm256_and_si256(_mm256_castps_si256(upper), _mm256_set1_epi32(INT32_MIN)));
        auto minimum = _mm256_set1_epi32(std::is_signed_v<To> ? INT32_MIN : 0);
        auto maximum = _mm256_set1_epi32(std::is_signed_v<To> ? INT32_MAX : -1);
        value = _mm256_blendv_epi8(value, minimum, _mm256_castps_si256(low));
        value = _mm256_blendv_epi8(value, maximum, _mm256_castps_si256(high));
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), value);
    }
};
} // namespace lutils::simd::detail
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
namespace lutils::simd::detail {
template <class To> struct FloatToIntegerNative<To, float, abi::neon> {
    static constexpr bool available =
        std::is_same_v<To, std::int32_t> || std::is_same_v<To, std::uint32_t>;
    static LUTILS_SIMD_INLINE void run(To *out, float const *in, Rounding mode) noexcept {
        auto x = vld1q_f32(in);
        x = vbslq_f32(vceqq_f32(x, x), x, vdupq_n_f32(0));
        if constexpr (std::is_signed_v<To>) {
            int32x4_t value;
            switch (mode) {
            case Rounding::NearestEven:
                value = vcvtnq_s32_f32(x);
                break;
            case Rounding::Downward:
                value = vcvtmq_s32_f32(x);
                break;
            case Rounding::Upward:
                value = vcvtpq_s32_f32(x);
                break;
            default:
                value = vcvtq_s32_f32(x);
                break;
            }
            vst1q_s32(out, value);
        } else {
            uint32x4_t value;
            switch (mode) {
            case Rounding::NearestEven:
                value = vcvtnq_u32_f32(x);
                break;
            case Rounding::Downward:
                value = vcvtmq_u32_f32(x);
                break;
            case Rounding::Upward:
                value = vcvtpq_u32_f32(x);
                break;
            default:
                value = vcvtq_u32_f32(x);
                break;
            }
            vst1q_u32(out, value);
        }
    }
};
} // namespace lutils::simd::detail
#endif
