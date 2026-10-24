#pragma once
#include <lutils/simd/Arithmetic.hpp>

namespace lutils::simd {
template <class T, class A> LUTILS_SIMD_INLINE T reduce_sum(Batch<T, A> value) noexcept {
    if constexpr (std::is_integral_v<T>)
        return detail::Ops<T, A>::reduceSum(detail::Access::data(value));
    T out{};
    for (std::size_t i = 0; i < value.size; ++i)
        out = detail::Number<T>::add(out, value.lane(i));
    return out;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE T reduce_min(Batch<T, A> value) noexcept {
    return detail::Ops<T, A>::reduceMin(detail::Access::data(value));
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE T reduce_max(Batch<T, A> value) noexcept {
    return detail::Ops<T, A>::reduceMax(detail::Access::data(value));
}
} // namespace lutils::simd
