#pragma once
#include <lutils/simd/Batch.hpp>
#include <lutils/simd/detail/MaskOps.hpp>
#include <optional>

namespace lutils::simd {
namespace detail {
struct MaskAccess;
LUTILS_SIMD_INLINE constexpr std::size_t countBits(std::uint64_t bits) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_popcountll(bits));
#else
    std::size_t result = 0;
    for (; bits; bits &= bits - 1)
        ++result;
    return result;
#endif
}
template <std::size_t Size>
inline constexpr std::uint64_t validMaskBits = ~std::uint64_t{0} >> (64 - Size);
}
template <class T, class Abi> class alignas(Batch<T, Abi>::alignment) Mask {
    static_assert(detail::element<T>);
    using U = detail::MaskLane<T>;
    static constexpr auto truth = std::numeric_limits<U>::max();

  public:
    static constexpr auto size = Batch<T, Abi>::size;
    static_assert(size > 0 && size <= 64);
    LUTILS_SIMD_INLINE constexpr Mask() noexcept = default;
    explicit LUTILS_SIMD_INLINE constexpr Mask(bool value) noexcept {
        for (auto &lane : lanes_)
            lane = value ? truth : U{};
    }
    LUTILS_SIMD_INLINE constexpr bool lane(std::size_t index) const noexcept {
        assert(index < size);
        return lanes_[index] != 0;
    }
    LUTILS_SIMD_INLINE constexpr void set_lane(std::size_t index, bool value) noexcept {
        assert(index < size);
        lanes_[index] = value ? truth : U{};
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator&(Mask a, Mask b) noexcept {
        for (std::size_t i = 0; i < size; ++i)
            a.lanes_[i] &= b.lanes_[i];
        return a;
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator|(Mask a, Mask b) noexcept {
        for (std::size_t i = 0; i < size; ++i)
            a.lanes_[i] |= b.lanes_[i];
        return a;
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator^(Mask a, Mask b) noexcept {
        for (std::size_t i = 0; i < size; ++i)
            a.lanes_[i] ^= b.lanes_[i];
        return a;
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator~(Mask a) noexcept {
        for (auto &lane : a.lanes_)
            lane = static_cast<U>(~lane);
        return a;
    }
    LUTILS_SIMD_INLINE constexpr Mask &operator&=(Mask b) noexcept { return *this = *this & b; }
    LUTILS_SIMD_INLINE constexpr Mask &operator|=(Mask b) noexcept { return *this = *this | b; }
    LUTILS_SIMD_INLINE constexpr Mask &operator^=(Mask b) noexcept { return *this = *this ^ b; }

  private:
    // Layout is independent of compiler ISA flags; constexpr needs no register type.
    std::array<U, size> lanes_{};
    friend struct detail::MaskAccess;
};
namespace detail {
struct MaskAccess {
    template <class T, class A>
    static LUTILS_SIMD_INLINE constexpr std::uint64_t bits(Mask<T, A> const &v) noexcept {
        if (!constantEvaluated())
            // Keep the logical width visible to range analysis (notably popcount
            // followed by a variable-size exact store).
            return MaskOps<T, A>::bits(v.lanes_.data()) & validMaskBits<Mask<T, A>::size>;
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < v.size; ++i)
            result |= std::uint64_t(v.lane(i)) << i;
        return result;
    }
    template <class T, class A>
    static LUTILS_SIMD_INLINE constexpr Mask<T, A> from(std::uint64_t value) noexcept {
        Mask<T, A> out;
        for (std::size_t i = 0; i < out.size; ++i)
            out.set_lane(i, ((value >> i) & 1) != 0);
        return out;
    }
    template <class T, class A>
    static LUTILS_SIMD_INLINE MaskLane<T> *data(Mask<T, A> &v) noexcept {
        return v.lanes_.data();
    }
    template <class T, class A>
    static LUTILS_SIMD_INLINE MaskLane<T> const *data(Mask<T, A> const &v) noexcept {
        return v.lanes_.data();
    }
};
} // namespace detail
template <class T, class A> LUTILS_SIMD_INLINE constexpr bool any(Mask<T, A> m) noexcept {
    return detail::MaskAccess::bits(m) != 0;
}
template <class T, class A> LUTILS_SIMD_INLINE constexpr bool none(Mask<T, A> m) noexcept {
    return !any(m);
}
template <class T, class A> LUTILS_SIMD_INLINE constexpr bool all(Mask<T, A> m) noexcept {
    return detail::MaskAccess::bits(m) == detail::validMaskBits<Mask<T, A>::size>;
}
template <class T, class A> LUTILS_SIMD_INLINE constexpr std::size_t count(Mask<T, A> m) noexcept {
    auto bits = detail::MaskAccess::bits(m);
#if defined(__GNUC__) || defined(__clang__)
    if (!detail::constantEvaluated())
        return static_cast<std::size_t>(__builtin_popcountll(bits));
#endif
    std::size_t result = 0;
    for (; bits; bits &= bits - 1)
        ++result;
    return result;
}
template <class T, class A>
LUTILS_SIMD_INLINE constexpr std::optional<std::size_t> first_true(Mask<T, A> m) noexcept {
    auto bits = detail::MaskAccess::bits(m);
    if (!bits)
        return std::nullopt;
#if defined(__GNUC__) || defined(__clang__)
    if (!detail::constantEvaluated())
        return static_cast<std::size_t>(__builtin_ctzll(bits));
#endif
    std::size_t i = 0;
    while (!(bits & 1)) {
        bits >>= 1;
        ++i;
    }
    return i;
}
template <class V>
LUTILS_SIMD_INLINE constexpr typename V::mask_type first_n(std::size_t n) noexcept {
    assert(n <= V::size);
    typename V::mask_type out;
    for (std::size_t i = 0; i < V::size; ++i)
        out.set_lane(i, i < n);
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> select(Mask<T, A> m, Batch<T, A> yes, Batch<T, A> no) noexcept {
    Batch<T, A> out;
    detail::Ops<T, A>::select(detail::Access::data(out), detail::MaskAccess::data(m),
                              detail::Access::data(yes), detail::Access::data(no));
    return out;
}
} // namespace lutils::simd
