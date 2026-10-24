#pragma once
#include <lutils/simd/detail/Vector.hpp>
#if defined(__AVX2__)
#include <immintrin.h>

namespace lutils::simd::detail {
template <class T> LUTILS_SIMD_INLINE std::uint64_t avxMask(__m256i v) noexcept {
    if constexpr (sizeof(T) == 4)
        return static_cast<unsigned>(_mm256_movemask_ps(_mm256_castsi256_ps(v)));
    if constexpr (sizeof(T) == 8)
        return static_cast<unsigned>(_mm256_movemask_pd(_mm256_castsi256_pd(v)));
    if constexpr (sizeof(T) == 2) {
        auto packed = _mm_packs_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
        return static_cast<unsigned>(_mm_movemask_epi8(packed));
    }
    return static_cast<unsigned>(_mm256_movemask_epi8(v));
}
#define LUTILS_AVX_BINARY(NAME, INTRINSIC)                                                         \
    static LUTILS_SIMD_INLINE void NAME(T *o, T const *a, T const *b) noexcept {                   \
        write(o, INTRINSIC(read(a), read(b)));                                                     \
    }
template <class T> struct AvxInteger : CompilerOps<T, abi::avx2> {
    static LUTILS_SIMD_INLINE __m256i read(T const *p) noexcept {
        return _mm256_loadu_si256(reinterpret_cast<__m256i const *>(p));
    }
    static LUTILS_SIMD_INLINE void write(T *p, __m256i v) noexcept {
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(p), v);
    }
    LUTILS_AVX_BINARY(bitAnd, _mm256_and_si256)
    LUTILS_AVX_BINARY(bitOr, _mm256_or_si256)
    LUTILS_AVX_BINARY(bitXor, _mm256_xor_si256)
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {
        if constexpr (sizeof(T) == 2) {
            auto x = read(a), y = read(b);
            auto l = _mm256_unpacklo_epi16(x, y), h = _mm256_unpackhi_epi16(x, y);
            write(lo, _mm256_permute2x128_si256(l, h, 0x20));
            write(hi, _mm256_permute2x128_si256(l, h, 0x31));
        } else
            CompilerOps<T, abi::avx2>::zip(lo, hi, a, b);
    }
};
template <class T, std::size_t Bytes> struct AvxWidth;
#define LUTILS_AVX_WIDTH(BYTES, SUFFIX, SIGN)                                                      \
    template <class T> struct AvxWidth<T, BYTES> : AvxInteger<T> {                                 \
        using AvxInteger<T>::read;                                                                 \
        using AvxInteger<T>::write;                                                                \
        LUTILS_AVX_BINARY(add, _mm256_add_epi##SUFFIX)                                             \
        LUTILS_AVX_BINARY(sub, _mm256_sub_epi##SUFFIX)                                             \
        static LUTILS_SIMD_INLINE void lessWide(MaskLane<T> *out, T const *a,                     \
                                                T const *b) noexcept {                           \
            auto x = read(a), y = read(b);                                                       \
            if constexpr (std::is_unsigned_v<T>) {                                              \
                x = _mm256_xor_si256(x, SIGN);                                                   \
                y = _mm256_xor_si256(y, SIGN);                                                   \
            }                                                                                   \
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(out),                                 \
                                _mm256_cmpgt_epi##SUFFIX(y, x));                                 \
        }                                                                                       \
    };
LUTILS_AVX_WIDTH(1, 8, _mm256_set1_epi8(-128))
LUTILS_AVX_WIDTH(2, 16, _mm256_set1_epi16(-32768))
LUTILS_AVX_WIDTH(4, 32, _mm256_set1_epi32(std::numeric_limits<int>::min()))
LUTILS_AVX_WIDTH(8, 64, _mm256_set1_epi64x(std::numeric_limits<long long>::min()))
#undef LUTILS_AVX_WIDTH
template <class T> struct AvxByte : AvxWidth<T, 1> {
    using AvxWidth<T, 1>::read;
    using AvxWidth<T, 1>::write;
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {
        auto x = read(a), y = read(b);
        auto l = _mm256_unpacklo_epi8(x, y), h = _mm256_unpackhi_epi8(x, y);
        write(lo, _mm256_permute2x128_si256(l, h, 0x20));
        write(hi, _mm256_permute2x128_si256(l, h, 0x31));
    }
    static LUTILS_SIMD_INLINE void unzip(T *even, T *odd, T const *a, T const *b) noexcept {
        auto x = read(a), y = read(b), mask = _mm256_set1_epi16(255);
        auto e = _mm256_packus_epi16(_mm256_and_si256(x, mask), _mm256_and_si256(y, mask));
        auto o = _mm256_packus_epi16(_mm256_srli_epi16(x, 8), _mm256_srli_epi16(y, 8));
        write(even, _mm256_permute4x64_epi64(e, _MM_SHUFFLE(3, 1, 2, 0)));
        write(odd, _mm256_permute4x64_epi64(o, _MM_SHUFFLE(3, 1, 2, 0)));
    }
};
template <> struct Ops<std::uint8_t, abi::avx2> : AvxByte<std::uint8_t> {
    using T = std::uint8_t;
    LUTILS_AVX_BINARY(average, _mm256_avg_epu8)
    LUTILS_AVX_BINARY(addSaturate, _mm256_adds_epu8)
    LUTILS_AVX_BINARY(subSaturate, _mm256_subs_epu8)
};
template <> struct Ops<std::int8_t, abi::avx2> : AvxByte<std::int8_t> {
    using T = std::int8_t;
    LUTILS_AVX_BINARY(addSaturate, _mm256_adds_epi8)
    LUTILS_AVX_BINARY(subSaturate, _mm256_subs_epi8)
};
template <> struct Ops<std::uint16_t, abi::avx2> : AvxWidth<std::uint16_t, 2> {
    using T = std::uint16_t;
    LUTILS_AVX_BINARY(mul, _mm256_mullo_epi16)
    LUTILS_AVX_BINARY(average, _mm256_avg_epu16)
    LUTILS_AVX_BINARY(addSaturate, _mm256_adds_epu16)
    LUTILS_AVX_BINARY(subSaturate, _mm256_subs_epu16)
};
template <> struct Ops<std::int16_t, abi::avx2> : AvxWidth<std::int16_t, 2> {
    using T = std::int16_t;
    LUTILS_AVX_BINARY(mul, _mm256_mullo_epi16)
    LUTILS_AVX_BINARY(addSaturate, _mm256_adds_epi16)
    LUTILS_AVX_BINARY(subSaturate, _mm256_subs_epi16)
};
template <> struct Ops<std::uint32_t, abi::avx2> : AvxWidth<std::uint32_t, 4> {
    using T = std::uint32_t;
    LUTILS_AVX_BINARY(mul, _mm256_mullo_epi32)
};
template <> struct Ops<std::int32_t, abi::avx2> : AvxWidth<std::int32_t, 4> {
    using T = std::int32_t;
    LUTILS_AVX_BINARY(mul, _mm256_mullo_epi32)
};
template <> struct Ops<std::uint64_t, abi::avx2> : AvxWidth<std::uint64_t, 8> {};
template <> struct Ops<std::int64_t, abi::avx2> : AvxWidth<std::int64_t, 8> {};
#define LUTILS_AVX_FLOAT(TYPE, REG, SUFFIX)                                                        \
    template <> struct Ops<TYPE, abi::avx2> : CompilerOps<TYPE, abi::avx2> {                       \
        using T = TYPE;                                                                            \
        static LUTILS_SIMD_INLINE REG read(T const *p) noexcept {                                  \
            return _mm256_loadu_##SUFFIX(p);                                                       \
        }                                                                                          \
        static LUTILS_SIMD_INLINE void write(T *p, REG v) noexcept {                               \
            _mm256_storeu_##SUFFIX(p, v);                                                          \
        }                                                                                          \
        LUTILS_AVX_BINARY(add, _mm256_add_##SUFFIX)                                                \
        LUTILS_AVX_BINARY(sub, _mm256_sub_##SUFFIX)                                                \
        LUTILS_AVX_BINARY(mul, _mm256_mul_##SUFFIX)                                                \
        LUTILS_AVX_BINARY(div, _mm256_div_##SUFFIX)                                                \
    };
LUTILS_AVX_FLOAT(float, __m256, ps)
LUTILS_AVX_FLOAT(double, __m256d, pd)
#undef LUTILS_AVX_FLOAT
#undef LUTILS_AVX_BINARY
} // namespace lutils::simd::detail
#endif
