#pragma once
#include <lutils/simd/detail/Vector.hpp>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>

namespace lutils::simd::detail {
template <class T> LUTILS_SIMD_INLINE std::uint64_t sseMask(__m128i v) noexcept {
    if constexpr (sizeof(T) == 4)
        return static_cast<unsigned>(_mm_movemask_ps(_mm_castsi128_ps(v)));
    if constexpr (sizeof(T) == 8)
        return static_cast<unsigned>(_mm_movemask_pd(_mm_castsi128_pd(v)));
    if constexpr (sizeof(T) == 2)
        return static_cast<unsigned>(_mm_movemask_epi8(_mm_packs_epi16(v, v))) & 255u;
    return static_cast<unsigned>(_mm_movemask_epi8(v));
}
#define LUTILS_SSE_BINARY(NAME, INTRINSIC)                                                         \
    static LUTILS_SIMD_INLINE void NAME(T *o, T const *a, T const *b) noexcept {                   \
        write(o, INTRINSIC(read(a), read(b)));                                                     \
    }
template <class T> struct SseInteger : CompilerOps<T, abi::sse2> {
    static LUTILS_SIMD_INLINE __m128i read(T const *p) noexcept {
        return _mm_loadu_si128(reinterpret_cast<__m128i const *>(p));
    }
    static LUTILS_SIMD_INLINE void write(T *p, __m128i v) noexcept {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(p), v);
    }
    LUTILS_SSE_BINARY(bitAnd, _mm_and_si128)
    LUTILS_SSE_BINARY(bitOr, _mm_or_si128)
    LUTILS_SSE_BINARY(bitXor, _mm_xor_si128)
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {
        if constexpr (sizeof(T) == 2) {
            auto x = read(a), y = read(b);
            write(lo, _mm_unpacklo_epi16(x, y));
            write(hi, _mm_unpackhi_epi16(x, y));
        } else
            CompilerOps<T, abi::sse2>::zip(lo, hi, a, b);
    }
};
template <class T, std::size_t Bytes> struct SseWidth;
#define LUTILS_SSE_WIDTH(BYTES, SUFFIX, SIGN)                                                      \
    template <class T> struct SseWidth<T, BYTES> : SseInteger<T> {                                 \
        using SseInteger<T>::read;                                                                 \
        using SseInteger<T>::write;                                                                \
        LUTILS_SSE_BINARY(add, _mm_add_epi##SUFFIX)                                                \
        LUTILS_SSE_BINARY(sub, _mm_sub_epi##SUFFIX)                                                \
        static LUTILS_SIMD_INLINE void lessWide(MaskLane<T> *out, T const *a,                     \
                                                T const *b) noexcept {                           \
            auto x = read(a), y = read(b);                                                       \
            if constexpr (std::is_unsigned_v<T>) {                                              \
                x = _mm_xor_si128(x, SIGN);                                                      \
                y = _mm_xor_si128(y, SIGN);                                                      \
            }                                                                                   \
            _mm_storeu_si128(reinterpret_cast<__m128i *>(out), _mm_cmpgt_epi##SUFFIX(y, x));      \
        }                                                                                       \
    };
LUTILS_SSE_WIDTH(1, 8, _mm_set1_epi8(-128))
LUTILS_SSE_WIDTH(2, 16, _mm_set1_epi16(-32768))
LUTILS_SSE_WIDTH(4, 32, _mm_set1_epi32(std::numeric_limits<int>::min()))
#undef LUTILS_SSE_WIDTH
template <class T> struct SseWidth<T, 8> : SseInteger<T> {
    using SseInteger<T>::read;
    using SseInteger<T>::write;
    LUTILS_SSE_BINARY(add, _mm_add_epi64)
    LUTILS_SSE_BINARY(sub, _mm_sub_epi64)
};
template <class T> struct SseByte : SseWidth<T, 1> {
    using SseWidth<T, 1>::read;
    using SseWidth<T, 1>::write;
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {
        auto x = read(a), y = read(b);
        write(lo, _mm_unpacklo_epi8(x, y));
        write(hi, _mm_unpackhi_epi8(x, y));
    }
    static LUTILS_SIMD_INLINE void unzip(T *even, T *odd, T const *a, T const *b) noexcept {
        auto x = read(a), y = read(b), mask = _mm_set1_epi16(255);
        write(even, _mm_packus_epi16(_mm_and_si128(x, mask), _mm_and_si128(y, mask)));
        write(odd, _mm_packus_epi16(_mm_srli_epi16(x, 8), _mm_srli_epi16(y, 8)));
    }
};
template <> struct Ops<std::uint8_t, abi::sse2> : SseByte<std::uint8_t> {
    using T = std::uint8_t;
    LUTILS_SSE_BINARY(average, _mm_avg_epu8)
    LUTILS_SSE_BINARY(addSaturate, _mm_adds_epu8)
    LUTILS_SSE_BINARY(subSaturate, _mm_subs_epu8)
};
template <> struct Ops<std::int8_t, abi::sse2> : SseByte<std::int8_t> {
    using T = std::int8_t;
    LUTILS_SSE_BINARY(addSaturate, _mm_adds_epi8)
    LUTILS_SSE_BINARY(subSaturate, _mm_subs_epi8)
};
template <> struct Ops<std::uint16_t, abi::sse2> : SseWidth<std::uint16_t, 2> {
    using T = std::uint16_t;
    LUTILS_SSE_BINARY(mul, _mm_mullo_epi16)
    LUTILS_SSE_BINARY(average, _mm_avg_epu16)
    LUTILS_SSE_BINARY(addSaturate, _mm_adds_epu16)
    LUTILS_SSE_BINARY(subSaturate, _mm_subs_epu16)
};
template <> struct Ops<std::int16_t, abi::sse2> : SseWidth<std::int16_t, 2> {
    using T = std::int16_t;
    LUTILS_SSE_BINARY(mul, _mm_mullo_epi16)
    LUTILS_SSE_BINARY(addSaturate, _mm_adds_epi16)
    LUTILS_SSE_BINARY(subSaturate, _mm_subs_epi16)
};
template <> struct Ops<std::uint32_t, abi::sse2> : SseWidth<std::uint32_t, 4> {};
template <> struct Ops<std::int32_t, abi::sse2> : SseWidth<std::int32_t, 4> {};
template <> struct Ops<std::uint64_t, abi::sse2> : SseWidth<std::uint64_t, 8> {};
template <> struct Ops<std::int64_t, abi::sse2> : SseWidth<std::int64_t, 8> {};

#define LUTILS_SSE_FLOAT(TYPE, REG, SUFFIX)                                                        \
    template <> struct Ops<TYPE, abi::sse2> : CompilerOps<TYPE, abi::sse2> {                       \
        using T = TYPE;                                                                            \
        static LUTILS_SIMD_INLINE REG read(T const *p) noexcept { return _mm_loadu_##SUFFIX(p); }  \
        static LUTILS_SIMD_INLINE void write(T *p, REG v) noexcept { _mm_storeu_##SUFFIX(p, v); }  \
        LUTILS_SSE_BINARY(add, _mm_add_##SUFFIX)                                                   \
        LUTILS_SSE_BINARY(sub, _mm_sub_##SUFFIX)                                                   \
        LUTILS_SSE_BINARY(mul, _mm_mul_##SUFFIX)                                                   \
        LUTILS_SSE_BINARY(div, _mm_div_##SUFFIX)                                                   \
    };
LUTILS_SSE_FLOAT(float, __m128, ps)
LUTILS_SSE_FLOAT(double, __m128d, pd)
#undef LUTILS_SSE_FLOAT
#undef LUTILS_SSE_BINARY
} // namespace lutils::simd::detail
#endif
