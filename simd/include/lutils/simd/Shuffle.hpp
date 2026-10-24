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
LUTILS_SIMD_INLINE Interleaved<Batch<T, A>> interleave(Batch<T, A> const &a,
                                                       Batch<T, A> const &b) noexcept {
    Interleaved<Batch<T, A>> out;
    detail::Ops<T, A>::zip(detail::Access::data(out.low), detail::Access::data(out.high),
                           detail::Access::data(a), detail::Access::data(b));
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE Deinterleaved<Batch<T, A>> deinterleave(Batch<T, A> const &a,
                                                           Batch<T, A> const &b) noexcept {
    Deinterleaved<Batch<T, A>> out;
    detail::Ops<T, A>::unzip(detail::Access::data(out.even), detail::Access::data(out.odd),
                             detail::Access::data(a), detail::Access::data(b));
    return out;
}
template <std::size_t... I, class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> shuffle(Batch<T, A> const &a) noexcept {
    static_assert(sizeof...(I) == Batch<T, A>::size, "shuffle must specify every output lane");
    static_assert(((I < Batch<T, A>::size) && ...), "shuffle index out of range");
    Batch<T, A> out;
#if defined(LUTILS_SIMD_COMPILER_VECTOR)
    using Native = typename detail::Shape<T, A>::native_abi;
    if constexpr (detail::compilerVector<Native> && Batch<T, A>::size == detail::lanes<T, Native>) {
        using Reg = detail::Vector<T, Native>;
        auto x = Reg::read(detail::Access::data(a));
        Reg::write(detail::Access::data(out), __builtin_shufflevector(x, x, I...));
        return out;
    }
#endif
    std::size_t index = 0;
    (out.set_lane(index++, a.lane(I)), ...);
    return out;
}
template <std::size_t N, class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> rotate_left(Batch<T, A> a) noexcept {
    Batch<T, A> out;
    for (std::size_t i = 0; i < Batch<T, A>::size; ++i)
        out.set_lane(i, a.lane((i + N % Batch<T, A>::size) % Batch<T, A>::size));
    return out;
}
namespace detail {
template <auto Index, std::size_t I> struct PermuteIndex {
    static constexpr auto raw = Index(I);
    static_assert(std::is_integral_v<decltype(raw)>, "permute index must be an integer");
    static_assert(!std::is_signed_v<decltype(raw)> || raw >= 0,
                  "permute index must be nonnegative");
    static constexpr auto value = static_cast<std::size_t>(raw);
};
template <std::size_t M, auto Index, class T, class A, std::size_t... I>
LUTILS_SIMD_INLINE auto permuteImpl(Batch<T, A> a, std::index_sequence<I...>) noexcept {
    static_assert(((PermuteIndex<Index, I>::value < Batch<T, A>::size) && ...),
                  "permute index out of range");
    Resized<T, M, A> out;
#if defined(LUTILS_SIMD_COMPILER_VECTOR)
    using Native = typename Shape<T, A>::native_abi;
    if constexpr (compilerVector<Native> && Batch<T, A>::size == lanes<T, Native> &&
                  M <= lanes<T, Native> && (M & (M - 1)) == 0) {
        using Reg = Vector<T, Native>;
        auto x = Reg::read(Access::data(a));
        auto shuffled = __builtin_shufflevector(x, x, PermuteIndex<Index, I>::value...);
        std::memcpy(Access::data(out), &shuffled, sizeof(T) * M);
        return out;
    }
#endif
    (out.set_lane(I, a.lane(PermuteIndex<Index, I>::value)), ...);
    return out;
}
template <std::size_t M, auto Index, class T, class A, std::size_t... I>
LUTILS_SIMD_INLINE auto permuteImpl(Batch<T, A> a, Batch<T, A> b,
                                    std::index_sequence<I...>) noexcept {
    static_assert(((PermuteIndex<Index, I>::value < 2 * Batch<T, A>::size) && ...),
                  "permute index out of range");
    Resized<T, M, A> out;
#if defined(LUTILS_SIMD_COMPILER_VECTOR)
    using Native = typename Shape<T, A>::native_abi;
    if constexpr (compilerVector<Native> && Batch<T, A>::size == lanes<T, Native> &&
                  M <= lanes<T, Native> && (M & (M - 1)) == 0) {
        using Reg = Vector<T, Native>;
        auto x = Reg::read(Access::data(a)), y = Reg::read(Access::data(b));
        auto shuffled = __builtin_shufflevector(x, y, PermuteIndex<Index, I>::value...);
        std::memcpy(Access::data(out), &shuffled, sizeof(T) * M);
        return out;
    }
#endif
    (out.set_lane(I, PermuteIndex<Index, I>::value < Batch<T, A>::size
                         ? a.lane(PermuteIndex<Index, I>::value)
                         : b.lane(PermuteIndex<Index, I>::value - Batch<T, A>::size)),
     ...);
    return out;
}
} // namespace detail
// Index is a C++17 constexpr function (or function pointer), evaluated per output lane.
template <std::size_t M, auto Index, class T, class A>
LUTILS_SIMD_INLINE auto permute(Batch<T, A> a) noexcept {
    static_assert(M > 0 && M <= 64, "permute requires 1..64 lanes");
    return detail::permuteImpl<M, Index>(a, std::make_index_sequence<M>{});
}
template <std::size_t M, auto Index, class T, class A>
LUTILS_SIMD_INLINE auto permute(Batch<T, A> a, Batch<T, A> b) noexcept {
    static_assert(M > 0 && M <= 64, "permute requires 1..64 lanes");
    return detail::permuteImpl<M, Index>(a, b, std::make_index_sequence<M>{});
}
template <class T, class A, class B>
LUTILS_SIMD_INLINE auto concat(Batch<T, A> a, Batch<T, B> b) noexcept {
    static_assert(std::is_same_v<typename detail::Shape<T, A>::native_abi,
                                 typename detail::Shape<T, B>::native_abi>,
                  "concat requires the same native ABI");
    detail::Resized<T, a.size + b.size, A> out;
    for (std::size_t i = 0; i < a.size; ++i)
        out.set_lane(i, a.lane(i));
    for (std::size_t i = 0; i < b.size; ++i)
        out.set_lane(a.size + i, b.lane(i));
    return out;
}
template <std::size_t Offset, std::size_t N, class T, class A>
LUTILS_SIMD_INLINE auto extract(Batch<T, A> value) noexcept {
    static_assert(N > 0 && Offset <= value.size && N <= value.size - Offset,
                  "extract range out of bounds");
    detail::Resized<T, N, A> out;
    for (std::size_t i = 0; i < N; ++i)
        out.set_lane(i, value.lane(Offset + i));
    return out;
}
template <class L, class H> struct Split {
    L low;
    H high;
};
template <std::size_t N, class T, class A>
LUTILS_SIMD_INLINE auto split(Batch<T, A> value) noexcept {
    static_assert(N > 0 && N < value.size, "split requires two nonempty parts");
    auto low = extract<0, N>(value);
    auto high = extract<N, value.size - N>(value);
    return Split<decltype(low), decltype(high)>{low, high};
}
} // namespace lutils::simd
