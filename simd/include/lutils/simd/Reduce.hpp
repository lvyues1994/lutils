#pragma once
#include <lutils/simd/Arithmetic.hpp>

namespace lutils::simd {
template <class T, class A> LUTILS_SIMD_INLINE T reduce_sum(Batch<T, A> value) noexcept {
    T out{};
    for (std::size_t i = 0; i < value.size; ++i)
        out = detail::Number<T>::add(out, value.lane(i));
    return out;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE T reduce_min(Batch<T, A> value) noexcept {
    T out = value.lane(0);
    for (std::size_t i = 1; i < value.size; ++i)
        if (value.lane(i) < out)
            out = value.lane(i);
    return out;
}
template <class T, class A, std::enable_if_t<std::is_integral_v<T>, int> = 0>
LUTILS_SIMD_INLINE T reduce_max(Batch<T, A> value) noexcept {
    T out = value.lane(0);
    for (std::size_t i = 1; i < value.size; ++i)
        if (value.lane(i) > out)
            out = value.lane(i);
    return out;
}
} // namespace lutils::simd
