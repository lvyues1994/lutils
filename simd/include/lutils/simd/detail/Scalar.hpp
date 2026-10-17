#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <lutils/simd/Abi.hpp>
#include <type_traits>

#if defined(__GNUC__) || defined(__clang__)
#define LUTILS_SIMD_INLINE inline __attribute__((always_inline))
#else
#define LUTILS_SIMD_INLINE inline
#endif

namespace lutils::simd::detail {
template <class T>
inline constexpr bool element =
    std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::uint8_t> ||
    std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t> ||
    std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
    std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> ||
    std::is_same_v<T, float> || std::is_same_v<T, double>;

template <class T, class Abi>
inline constexpr std::size_t lanes = Abi::bytes == 0 ? 1 : Abi::bytes / sizeof(T);

template <class To, class From> LUTILS_SIMD_INLINE To bits(From value) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>);
    To out;
    std::memcpy(&out, &value, sizeof(To));
    return out;
}
template <class T> LUTILS_SIMD_INLINE T wrap(std::uint64_t value) noexcept {
    using U = std::make_unsigned_t<T>;
    return bits<T>(static_cast<U>(value));
}
template <class T, bool = std::is_integral_v<T>> struct Number;
template <class T> struct Number<T, true> {
    using U = std::make_unsigned_t<T>;
    static LUTILS_SIMD_INLINE std::uint64_t unsignedValue(T a) noexcept {
        return static_cast<U>(a);
    }
    static LUTILS_SIMD_INLINE T add(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) + unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T sub(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) - unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T mul(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) * unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T bitAnd(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) & unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T bitOr(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) | unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T bitXor(T a, T b) noexcept {
        return wrap<T>(unsignedValue(a) ^ unsignedValue(b));
    }
    static LUTILS_SIMD_INLINE T average(T a, T b) noexcept {
        static_assert(std::is_unsigned_v<T>);
        return static_cast<T>((unsignedValue(a) | unsignedValue(b)) -
                              ((unsignedValue(a) ^ unsignedValue(b)) >> 1));
    }
    static LUTILS_SIMD_INLINE T addSaturate(T a, T b) noexcept {
        constexpr T lo = std::numeric_limits<T>::lowest(), hi = std::numeric_limits<T>::max();
        if constexpr (std::is_unsigned_v<T>) {
            return b > static_cast<T>(hi - a) ? hi : static_cast<T>(a + b);
        } else {
            if (b > 0 && a > static_cast<T>(hi - b))
                return hi;
            if (b < 0 && a < static_cast<T>(lo - b))
                return lo;
            return static_cast<T>(a + b);
        }
    }
    static LUTILS_SIMD_INLINE T subSaturate(T a, T b) noexcept {
        constexpr T lo = std::numeric_limits<T>::lowest(), hi = std::numeric_limits<T>::max();
        if constexpr (std::is_unsigned_v<T>) {
            return a < b ? T{} : static_cast<T>(a - b);
        } else {
            if (b > 0 && a < static_cast<T>(lo + b))
                return lo;
            if (b < 0 && a > static_cast<T>(hi + b))
                return hi;
            return static_cast<T>(a - b);
        }
    }
};
template <class T> struct Number<T, false> {
    static LUTILS_SIMD_INLINE T add(T a, T b) noexcept { return a + b; }
    static LUTILS_SIMD_INLINE T sub(T a, T b) noexcept { return a - b; }
    static LUTILS_SIMD_INLINE T mul(T a, T b) noexcept { return a * b; }
    static LUTILS_SIMD_INLINE T div(T a, T b) noexcept { return a / b; }
};

// Correctness fallback for operations without a native implementation.
template <class T, class Abi> struct Emulated {
    static constexpr auto size = lanes<T, Abi>;
#define LUTILS_SIMD_FALLBACK(NAME)                                                                 \
    static LUTILS_SIMD_INLINE void NAME(T *out, T const *a, T const *b) noexcept {                 \
        for (std::size_t i = 0; i < size; ++i)                                                     \
            out[i] = Number<T>::NAME(a[i], b[i]);                                                  \
    }
    LUTILS_SIMD_FALLBACK(add)
    LUTILS_SIMD_FALLBACK(sub)
    LUTILS_SIMD_FALLBACK(mul)
    LUTILS_SIMD_FALLBACK(div)
    LUTILS_SIMD_FALLBACK(bitAnd)
    LUTILS_SIMD_FALLBACK(bitOr)
    LUTILS_SIMD_FALLBACK(bitXor)
    LUTILS_SIMD_FALLBACK(average)
    LUTILS_SIMD_FALLBACK(addSaturate)
    LUTILS_SIMD_FALLBACK(subSaturate)
#undef LUTILS_SIMD_FALLBACK
    static LUTILS_SIMD_INLINE std::uint64_t equal(T const *a, T const *b) noexcept {
        std::uint64_t out = 0;
        for (std::size_t i = 0; i < size; ++i)
            out |= std::uint64_t(a[i] == b[i]) << i;
        return out;
    }
    static LUTILS_SIMD_INLINE std::uint64_t less(T const *a, T const *b) noexcept {
        std::uint64_t out = 0;
        for (std::size_t i = 0; i < size; ++i)
            out |= std::uint64_t(a[i] < b[i]) << i;
        return out;
    }
    static LUTILS_SIMD_INLINE void unzip(T *even, T *odd, T const *a, T const *b) noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            auto index = 2 * i;
            even[i] = index < size ? a[index] : b[index - size];
            ++index;
            odd[i] = index < size ? a[index] : b[index - size];
        }
    }
    static LUTILS_SIMD_INLINE void zip(T *low, T *high, T const *a, T const *b) noexcept {
        for (std::size_t i = 0; i < size; ++i) {
            auto index = 2 * i;
            (index < size ? low[index] : high[index - size]) = a[i];
            ++index;
            (index < size ? low[index] : high[index - size]) = b[i];
        }
    }
};
template <class T, class Abi> struct Ops;
template <class T> struct Ops<T, abi::scalar> : Emulated<T, abi::scalar> {};
} // namespace lutils::simd::detail
