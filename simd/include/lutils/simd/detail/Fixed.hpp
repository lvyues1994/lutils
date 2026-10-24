#pragma once
#include <lutils/simd/detail/Avx2.hpp>
#include <lutils/simd/detail/Neon.hpp>
#include <lutils/simd/detail/Sse2.hpp>

namespace lutils::simd::detail {
template <class T, std::size_t N, class A>
struct Ops<T, abi::fixed<N, A>> : Emulated<T, abi::fixed<N, A>> {
    using U = MaskLane<T>;
    static constexpr auto width = lanes<T, A>;
    // Tail lanes are evaluated individually: padding a floating divide with
    // zeros would introduce exceptions from lanes that are not part of the value.
#define LUTILS_FIXED_BINARY(NAME)                                                                  \
    static LUTILS_SIMD_INLINE void NAME(T *out, T const *a, T const *b) noexcept {                 \
        std::size_t i = 0;                                                                         \
        for (; N - i >= width; i += width)                                                         \
            Ops<T, A>::NAME(out + i, a + i, b + i);                                                \
        for (; i < N; ++i)                                                                         \
            Ops<T, abi::scalar>::NAME(out + i, a + i, b + i);                                      \
    }
    LUTILS_FIXED_BINARY(add)
    LUTILS_FIXED_BINARY(sub)
    LUTILS_FIXED_BINARY(mul)
    LUTILS_FIXED_BINARY(div)
    LUTILS_FIXED_BINARY(bitAnd)
    LUTILS_FIXED_BINARY(bitOr)
    LUTILS_FIXED_BINARY(bitXor)
    LUTILS_FIXED_BINARY(average)
    LUTILS_FIXED_BINARY(addSaturate)
    LUTILS_FIXED_BINARY(subSaturate)
    LUTILS_FIXED_BINARY(min)
    LUTILS_FIXED_BINARY(max)
#undef LUTILS_FIXED_BINARY
#define LUTILS_FIXED_COMPARE(NAME)                                                                 \
    static LUTILS_SIMD_INLINE void NAME(U *out, T const *a, T const *b) noexcept {                 \
        std::size_t i = 0;                                                                         \
        for (; N - i >= width; i += width)                                                         \
            Ops<T, A>::NAME(out + i, a + i, b + i);                                                \
        for (; i < N; ++i)                                                                         \
            Ops<T, abi::scalar>::NAME(out + i, a + i, b + i);                                      \
    }
    LUTILS_FIXED_COMPARE(equalWide)
    LUTILS_FIXED_COMPARE(lessWide)
#undef LUTILS_FIXED_COMPARE
    static LUTILS_SIMD_INLINE void select(T *out, U const *m, T const *a, T const *b) noexcept {
        std::size_t i = 0;
        for (; N - i >= width; i += width)
            Ops<T, A>::select(out + i, m + i, a + i, b + i);
        for (; i < N; ++i)
            out[i] = m[i] ? a[i] : b[i];
    }
    static LUTILS_SIMD_INLINE T reduceSum(T const *a) noexcept {
        T out{};
        std::size_t i = 0;
        for (; N - i >= width; i += width)
            out = Number<T>::add(out, Ops<T, A>::reduceSum(a + i));
        for (; i < N; ++i)
            out = Number<T>::add(out, a[i]);
        return out;
    }
    static LUTILS_SIMD_INLINE T reduceMin(T const *a) noexcept {
        T out = a[0];
        std::size_t i = 0;
        for (; N - i >= width; i += width) {
            auto x = Ops<T, A>::reduceMin(a + i);
            out = x < out ? x : out;
        }
        for (; i < N; ++i)
            out = a[i] < out ? a[i] : out;
        return out;
    }
    static LUTILS_SIMD_INLINE T reduceMax(T const *a) noexcept {
        T out = a[0];
        std::size_t i = 0;
        for (; N - i >= width; i += width) {
            auto x = Ops<T, A>::reduceMax(a + i);
            out = x > out ? x : out;
        }
        for (; i < N; ++i)
            out = a[i] > out ? a[i] : out;
        return out;
    }
};
} // namespace lutils::simd::detail
