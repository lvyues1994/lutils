#pragma once
#include <lutils/simd/detail/Scalar.hpp>
#if defined(__aarch64__)
#include <arm_neon.h>

namespace lutils::simd::detail {
#define LUTILS_NEON_BINARY(NAME, INTRINSIC)                                                        \
    static LUTILS_SIMD_INLINE void NAME(T *o, T const *a, T const *b) noexcept {                   \
        write(o, INTRINSIC(read(a), read(b)));                                                     \
    }
#define LUTILS_NEON_MUL(S) LUTILS_NEON_BINARY(mul, vmulq_##S)
#define LUTILS_NEON_AVG(S) LUTILS_NEON_BINARY(average, vrhaddq_##S)
// A comparison mask is stored to unsigned lanes before compacting; no x86 mask layout leaks out.
#define LUTILS_NEON_BODY(TYPE, REG, SUFFIX, BITS)                                                  \
    using T = TYPE;                                                                                \
    static LUTILS_SIMD_INLINE REG read(T const *p) noexcept { return vld1q_##SUFFIX(p); }          \
    static LUTILS_SIMD_INLINE void write(T *p, REG v) noexcept { vst1q_##SUFFIX(p, v); }           \
    static LUTILS_SIMD_INLINE std::uint64_t mask(                                                  \
        decltype(vceqq_##SUFFIX(read(nullptr), read(nullptr))) m) noexcept {                       \
        std::uint##BITS##_t values[16 / sizeof(T)];                                                \
        vst1q_u##BITS(values, m);                                                                  \
        std::uint64_t out = 0;                                                                     \
        for (std::size_t i = 0; i < 16 / sizeof(T); ++i)                                           \
            out |= std::uint64_t(values[i] != 0) << i;                                             \
        return out;                                                                                \
    }                                                                                              \
    LUTILS_NEON_BINARY(add, vaddq_##SUFFIX)                                                        \
    LUTILS_NEON_BINARY(sub, vsubq_##SUFFIX)                                                        \
    static LUTILS_SIMD_INLINE std::uint64_t equal(T const *a, T const *b) noexcept {               \
        return mask(vceqq_##SUFFIX(read(a), read(b)));                                             \
    }                                                                                              \
    static LUTILS_SIMD_INLINE std::uint64_t less(T const *a, T const *b) noexcept {                \
        return mask(vcltq_##SUFFIX(read(a), read(b)));                                             \
    }                                                                                              \
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {            \
        auto x = read(a), y = read(b);                                                             \
        write(lo, vzip1q_##SUFFIX(x, y));                                                          \
        write(hi, vzip2q_##SUFFIX(x, y));                                                          \
    }                                                                                              \
    static LUTILS_SIMD_INLINE void unzip(T *even, T *odd, T const *a, T const *b) noexcept {       \
        auto x = read(a), y = read(b);                                                             \
        write(even, vuzp1q_##SUFFIX(x, y));                                                        \
        write(odd, vuzp2q_##SUFFIX(x, y));                                                         \
    }
#define LUTILS_NEON_INTEGER(TYPE, REG, SUFFIX, BITS, MUL, AVG)                                     \
    template <> struct Ops<TYPE, abi::neon> : Emulated<TYPE, abi::neon> {                          \
        LUTILS_NEON_BODY(TYPE, REG, SUFFIX, BITS)                                                  \
        LUTILS_NEON_BINARY(bitAnd, vandq_##SUFFIX)                                                 \
        LUTILS_NEON_BINARY(bitOr, vorrq_##SUFFIX)                                                  \
        LUTILS_NEON_BINARY(bitXor, veorq_##SUFFIX)                                                 \
        LUTILS_NEON_BINARY(addSaturate, vqaddq_##SUFFIX)                                           \
        LUTILS_NEON_BINARY(subSaturate, vqsubq_##SUFFIX)                                           \
        MUL AVG                                                                                    \
    };
LUTILS_NEON_INTEGER(std::uint8_t, uint8x16_t, u8, 8, LUTILS_NEON_MUL(u8), LUTILS_NEON_AVG(u8))
LUTILS_NEON_INTEGER(std::int8_t, int8x16_t, s8, 8, LUTILS_NEON_MUL(s8), )
LUTILS_NEON_INTEGER(std::uint16_t, uint16x8_t, u16, 16, LUTILS_NEON_MUL(u16), LUTILS_NEON_AVG(u16))
LUTILS_NEON_INTEGER(std::int16_t, int16x8_t, s16, 16, LUTILS_NEON_MUL(s16), )
LUTILS_NEON_INTEGER(std::uint32_t, uint32x4_t, u32, 32, LUTILS_NEON_MUL(u32), LUTILS_NEON_AVG(u32))
LUTILS_NEON_INTEGER(std::int32_t, int32x4_t, s32, 32, LUTILS_NEON_MUL(s32), )
LUTILS_NEON_INTEGER(std::uint64_t, uint64x2_t, u64, 64, , )
LUTILS_NEON_INTEGER(std::int64_t, int64x2_t, s64, 64, , )
#define LUTILS_NEON_FLOAT(TYPE, REG, SUFFIX, BITS)                                                 \
    template <> struct Ops<TYPE, abi::neon> : Emulated<TYPE, abi::neon> {                          \
        LUTILS_NEON_BODY(TYPE, REG, SUFFIX, BITS)                                                  \
        LUTILS_NEON_MUL(SUFFIX)                                                                    \
        LUTILS_NEON_BINARY(div, vdivq_##SUFFIX)                                                    \
    };
LUTILS_NEON_FLOAT(float, float32x4_t, f32, 32)
LUTILS_NEON_FLOAT(double, float64x2_t, f64, 64)
#undef LUTILS_NEON_FLOAT
#undef LUTILS_NEON_INTEGER
#undef LUTILS_NEON_BODY
#undef LUTILS_NEON_AVG
#undef LUTILS_NEON_MUL
#undef LUTILS_NEON_BINARY
} // namespace lutils::simd::detail
#endif
