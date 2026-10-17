#pragma once
#include <lutils/simd/Arithmetic.hpp>

namespace lutils::simd {
template <class V> struct Interleaved {
    V low;
    V high;
};
template <class V> struct Deinterleaved {
    V even;
    V odd;
};
template <class T, class A>
LUTILS_SIMD_INLINE Interleaved<Batch<T, A>> interleave(Batch<T, A> a,
                                                              Batch<T, A> b) noexcept {
    Interleaved<Batch<T, A>> out;
    detail::Ops<T, A>::zip(detail::Access::data(out.low), detail::Access::data(out.high),
                           detail::Access::data(a), detail::Access::data(b));
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE Deinterleaved<Batch<T, A>> deinterleave(Batch<T, A> a,
                                                                  Batch<T, A> b) noexcept {
    Deinterleaved<Batch<T, A>> out;
    detail::Ops<T, A>::unzip(detail::Access::data(out.even), detail::Access::data(out.odd),
                             detail::Access::data(a), detail::Access::data(b));
    return out;
}
template <std::size_t... I, class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> shuffle(Batch<T, A> a) noexcept {
    static_assert(sizeof...(I) == Batch<T, A>::size, "shuffle must specify every output lane");
    static_assert(((I < Batch<T, A>::size) && ...), "shuffle index out of range");
    Batch<T, A> out;
    std::size_t index = 0;
    (out.set_lane(index++, a.lane(I)), ...);
    return out;
}
template <std::size_t N, class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> rotate_left(Batch<T, A> a) noexcept {
    Batch<T, A> out;
    for (std::size_t i = 0; i < a.size; ++i)
        out.set_lane(i, a.lane((i + N % a.size) % a.size));
    return out;
}
} // namespace lutils::simd
