#pragma once
#include <cmath>
#include <lutils/simd/Mask.hpp>
#include <lutils/simd/detail/Avx2.hpp>
#include <lutils/simd/detail/Neon.hpp>
#include <lutils/simd/detail/Shifts.hpp>
#include <lutils/simd/detail/Sse2.hpp>

namespace lutils::simd {
#define LUTILS_SIMD_BINARY(OP, NAME, CONDITION)                                                    \
    template <class T, class A, std::enable_if_t<(CONDITION), int> = 0>                            \
    LUTILS_SIMD_INLINE Batch<T, A> operator OP(Batch<T, A> a, Batch<T, A> b) noexcept {            \
        Batch<T, A> out;                                                                           \
        detail::Ops<T, A>::NAME(detail::Access::data(out), detail::Access::data(a),                \
                                detail::Access::data(b));                                          \
        return out;                                                                                \
    }                                                                                              \
    template <class T, class A, std::enable_if_t<(CONDITION), int> = 0>                            \
    LUTILS_SIMD_INLINE Batch<T, A> &operator OP##=(Batch<T, A> &a, Batch<T, A> b) noexcept {       \
        return a = a OP b;                                                                         \
    }
LUTILS_SIMD_BINARY(+, add, true)
LUTILS_SIMD_BINARY(-, sub, true)
LUTILS_SIMD_BINARY(*, mul, true)
LUTILS_SIMD_BINARY(/, div, std::is_floating_point_v<T>)
LUTILS_SIMD_BINARY(&, bitAnd, std::is_integral_v<T>)
LUTILS_SIMD_BINARY(|, bitOr, std::is_integral_v<T>)
LUTILS_SIMD_BINARY(^, bitXor, std::is_integral_v<T>)
#undef LUTILS_SIMD_BINARY
template <class T, class A> LUTILS_SIMD_INLINE Batch<T, A> operator-(Batch<T, A> v) noexcept {
    if constexpr (std::is_floating_point_v<T>) {
        for (std::size_t i = 0; i < v.size; ++i)
            v.set_lane(i, -v.lane(i));
        return v;
    } else
        return Batch<T, A>{} - v;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE Batch<T, A> operator~(Batch<T, A> v) noexcept {
    return v ^ Batch<T, A>{detail::wrap<T>(~std::uint64_t{0})};
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator==(Batch<T, A> a, Batch<T, A> b) noexcept {
    Mask<T, A> out;
    detail::Ops<T, A>::equalWide(detail::MaskAccess::data(out), detail::Access::data(a),
                                 detail::Access::data(b));
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator<(Batch<T, A> a, Batch<T, A> b) noexcept {
    Mask<T, A> out;
    detail::Ops<T, A>::lessWide(detail::MaskAccess::data(out), detail::Access::data(a),
                                detail::Access::data(b));
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator!=(Batch<T, A> a, Batch<T, A> b) noexcept {
    return ~(a == b);
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator>(Batch<T, A> a, Batch<T, A> b) noexcept {
    return b < a;
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator<=(Batch<T, A> a, Batch<T, A> b) noexcept {
    return (a < b) | (a == b);
}
template <class T, class A>
LUTILS_SIMD_INLINE Mask<T, A> operator>=(Batch<T, A> a, Batch<T, A> b) noexcept {
    return (a > b) | (a == b);
}
#define LUTILS_SIMD_NAMED(NAME, BACKEND, CONDITION)                                                \
    template <class T, class A, std::enable_if_t<(CONDITION), int> = 0>                            \
    LUTILS_SIMD_INLINE Batch<T, A> NAME(Batch<T, A> a, Batch<T, A> b) noexcept {                   \
        Batch<T, A> out;                                                                           \
        detail::Ops<T, A>::BACKEND(detail::Access::data(out), detail::Access::data(a),             \
                                   detail::Access::data(b));                                       \
        return out;                                                                                \
    }
LUTILS_SIMD_NAMED(add_saturate, addSaturate, std::is_integral_v<T>)
LUTILS_SIMD_NAMED(sub_saturate, subSaturate, std::is_integral_v<T>)
LUTILS_SIMD_NAMED(average_round_up, average, std::is_unsigned_v<T>)
LUTILS_SIMD_NAMED(min, min, std::is_integral_v<T>)
LUTILS_SIMD_NAMED(max, max, std::is_integral_v<T>)
#undef LUTILS_SIMD_NAMED
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE Batch<T, A> shift_left(Batch<T, A> a, unsigned n) noexcept {
    Batch<T, A> out;
    if (n >= sizeof(T) * 8)
        return out;
    for (std::size_t i = 0; i < a.size; ++i)
        out.set_lane(i, detail::wrap<T>(detail::Number<T>::unsignedValue(a.lane(i)) << n));
    return out;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE Batch<T, A> shift_right_logical(Batch<T, A> a, unsigned n) noexcept {
    Batch<T, A> out;
    if (n >= sizeof(T) * 8)
        return out;
    for (std::size_t i = 0; i < a.size; ++i)
        out.set_lane(i, detail::wrap<T>(detail::Number<T>::unsignedValue(a.lane(i)) >> n));
    return out;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, int> = 0>
LUTILS_SIMD_INLINE Batch<T, A> shift_right_arithmetic(Batch<T, A> a, unsigned n) noexcept {
    Batch<T, A> out;
    if constexpr (detail::ArithmeticShift<T, A>::available) {
        detail::ArithmeticShift<T, A>::run(detail::Access::data(out), detail::Access::data(a), n);
        return out;
    }
    constexpr unsigned width = sizeof(T) * 8;
    if (n == 0)
        return a;
    for (std::size_t i = 0; i < a.size; ++i) {
        auto negative = a.lane(i) < 0;
        auto bits = detail::Number<T>::unsignedValue(a.lane(i));
        auto shifted = n >= width ? std::uint64_t{0} : bits >> n;
        if (negative)
            shifted |= n >= width ? ~std::uint64_t{0} : (~std::uint64_t{0} << (width - n));
        out.set_lane(i, detail::wrap<T>(shifted));
    }
    return out;
}
template <class T, class A, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
LUTILS_SIMD_INLINE Batch<T, A> fma(Batch<T, A> a, Batch<T, A> b, Batch<T, A> c) noexcept {
    Batch<T, A> out;
    for (std::size_t i = 0; i < a.size; ++i)
        out.set_lane(i, std::fma(a.lane(i), b.lane(i), c.lane(i)));
    return out;
}
} // namespace lutils::simd
