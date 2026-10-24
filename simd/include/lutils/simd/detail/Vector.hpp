#pragma once
#include <lutils/simd/detail/Scalar.hpp>
#include <utility>

namespace lutils::simd::detail {
template <class A> inline constexpr bool compilerVector = false;
#if !defined(LUTILS_SIMD_DISABLE_COMPILER_VECTOR) && defined(__has_builtin)
#if __has_builtin(__builtin_shufflevector) && (defined(__GNUC__) || defined(__clang__))
#if defined(__SSE2__)
template <> inline constexpr bool compilerVector<abi::sse2> = true;
#endif
#if defined(__AVX2__)
template <> inline constexpr bool compilerVector<abi::avx2> = true;
#endif
#if defined(__aarch64__)
template <> inline constexpr bool compilerVector<abi::neon> = true;
#endif
#define LUTILS_SIMD_COMPILER_VECTOR 1
#endif
#endif

template <class T, class A, bool = compilerVector<A>> struct CompilerOps : Emulated<T, A> {};
#if defined(LUTILS_SIMD_COMPILER_VECTOR)
// Private compiler types never cross the public pointer-based dispatch boundary.
template <class T, class A> struct Vector {
    typedef T Type __attribute__((vector_size(A::bytes)));
    static LUTILS_SIMD_INLINE Type read(void const *p) noexcept {
        Type v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    static LUTILS_SIMD_INLINE void write(void *p, Type v) noexcept {
        std::memcpy(p, &v, sizeof(v));
    }
};
template <class T, class A> struct CompilerOps<T, A, true> : Emulated<T, A> {
    using U = MaskLane<T>;
    using V = Vector<T, A>;
    using Bits = Vector<U, A>;
    static constexpr auto size = lanes<T, A>;
    static LUTILS_SIMD_INLINE void equalWide(U *out, T const *a, T const *b) noexcept {
        auto mask = V::read(a) == V::read(b);
        std::memcpy(out, &mask, sizeof(mask));
    }
    static LUTILS_SIMD_INLINE void lessWide(U *out, T const *a, T const *b) noexcept {
        auto mask = V::read(a) < V::read(b);
        std::memcpy(out, &mask, sizeof(mask));
    }
    static LUTILS_SIMD_INLINE void select(T *out, U const *mask, T const *a, T const *b) noexcept {
        auto m = Bits::read(mask);
        Bits::write(out, (m & Bits::read(a)) | (~m & Bits::read(b)));
    }
    static LUTILS_SIMD_INLINE void min(T *out, T const *a, T const *b) noexcept {
        auto x = V::read(a), y = V::read(b);
        V::write(out, x < y ? x : y);
    }
    static LUTILS_SIMD_INLINE void max(T *out, T const *a, T const *b) noexcept {
        auto x = V::read(a), y = V::read(b);
        V::write(out, x > y ? x : y);
    }
    template <std::size_t... I>
    static LUTILS_SIMD_INLINE void zipImpl(T *lo, T *hi, T const *a, T const *b,
                                           std::index_sequence<I...>) noexcept {
        auto x = V::read(a), y = V::read(b);
        V::write(lo, __builtin_shufflevector(x, y, (I / 2 + (I % 2) * size)...));
        V::write(hi, __builtin_shufflevector(x, y, ((I + size) / 2 + (I % 2) * size)...));
    }
    static LUTILS_SIMD_INLINE void zip(T *lo, T *hi, T const *a, T const *b) noexcept {
        zipImpl(lo, hi, a, b, std::make_index_sequence<size>{});
    }
    template <std::size_t... I>
    static LUTILS_SIMD_INLINE void unzipImpl(T *even, T *odd, T const *a, T const *b,
                                             std::index_sequence<I...>) noexcept {
        auto x = V::read(a), y = V::read(b);
        V::write(even, __builtin_shufflevector(x, y, (2 * I)...));
        V::write(odd, __builtin_shufflevector(x, y, (2 * I + 1)...));
    }
    static LUTILS_SIMD_INLINE void unzip(T *even, T *odd, T const *a, T const *b) noexcept {
        unzipImpl(even, odd, a, b, std::make_index_sequence<size>{});
    }
    template <std::size_t Step, class R, std::size_t... I>
    static LUTILS_SIMD_INLINE R swap(R x, std::index_sequence<I...>) noexcept {
        return __builtin_shufflevector(x, x, (I ^ Step)...);
    }
    template <std::size_t Step, int Operation, class R>
    static LUTILS_SIMD_INLINE auto reduce(R x) noexcept {
        if constexpr (Step == 0)
            return x[0];
        else {
            auto y = swap<Step>(x, std::make_index_sequence<size>{});
            if constexpr (Operation == 0)
                return reduce<Step / 2, Operation>(x + y);
            else if constexpr (Operation == 1)
                return reduce<Step / 2, Operation>(x < y ? x : y);
            else
                return reduce<Step / 2, Operation>(x > y ? x : y);
        }
    }
    static LUTILS_SIMD_INLINE T reduceSum(T const *a) noexcept {
        // Unsigned arithmetic preserves the modular contract, including signed input.
        static_assert(std::is_integral_v<T>);
        return bits<T>(reduce<size / 2, 0>(Bits::read(a)));
    }
    static LUTILS_SIMD_INLINE T reduceMin(T const *a) noexcept {
        return reduce<size / 2, 1>(V::read(a));
    }
    static LUTILS_SIMD_INLINE T reduceMax(T const *a) noexcept {
        return reduce<size / 2, 2>(V::read(a));
    }
};
#endif
} // namespace lutils::simd::detail
