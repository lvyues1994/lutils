#pragma once
#include <lutils/simd/Batch.hpp>
#include <optional>

namespace lutils::simd {
namespace detail {
struct MaskAccess;
}
template <class T, class Abi> class Mask {
    static_assert(detail::element<T>);

  public:
    static constexpr auto size = Batch<T, Abi>::size;
    static_assert(size > 0 && size < 64);
    LUTILS_SIMD_INLINE constexpr Mask() noexcept = default;
    explicit LUTILS_SIMD_INLINE constexpr Mask(bool value) noexcept : bits_(value ? valid : 0) {}
    LUTILS_SIMD_INLINE constexpr bool lane(std::size_t index) const noexcept {
        assert(index < size);
        return ((bits_ >> index) & 1) != 0;
    }
    LUTILS_SIMD_INLINE constexpr void set_lane(std::size_t index, bool value) noexcept {
        assert(index < size);
        auto bit = std::uint64_t{1} << index;
        bits_ = value ? bits_ | bit : bits_ & ~bit;
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator&(Mask a, Mask b) noexcept {
        return from(a.bits_ & b.bits_);
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator|(Mask a, Mask b) noexcept {
        return from(a.bits_ | b.bits_);
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator^(Mask a, Mask b) noexcept {
        return from(a.bits_ ^ b.bits_);
    }
    friend LUTILS_SIMD_INLINE constexpr Mask operator~(Mask a) noexcept { return from(~a.bits_); }
    LUTILS_SIMD_INLINE constexpr Mask &operator&=(Mask b) noexcept { return *this = *this & b; }
    LUTILS_SIMD_INLINE constexpr Mask &operator|=(Mask b) noexcept { return *this = *this | b; }
    LUTILS_SIMD_INLINE constexpr Mask &operator^=(Mask b) noexcept { return *this = *this ^ b; }

  private:
    static constexpr auto valid = (std::uint64_t{1} << size) - 1;
    static LUTILS_SIMD_INLINE constexpr Mask from(std::uint64_t value) noexcept {
        Mask out;
        out.bits_ = value & valid;
        return out;
    }
    std::uint64_t bits_ = 0;
    friend struct detail::MaskAccess;
};
namespace detail {
struct MaskAccess {
    template <class T, class A>
    static LUTILS_SIMD_INLINE constexpr std::uint64_t bits(Mask<T, A> v) noexcept {
        return v.bits_;
    }
    template <class T, class A>
    static LUTILS_SIMD_INLINE constexpr Mask<T, A> from(std::uint64_t value) noexcept {
        return Mask<T, A>::from(value);
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
    return none(~m);
}
template <class T, class A>
LUTILS_SIMD_INLINE constexpr std::size_t count(Mask<T, A> m) noexcept {
    auto bits = detail::MaskAccess::bits(m);
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
    return detail::MaskAccess::from<typename V::value_type, typename V::abi_type>(
        (std::uint64_t{1} << n) - 1);
}
template <class T, class A>
LUTILS_SIMD_INLINE Batch<T, A> select(Mask<T, A> m, Batch<T, A> yes,
                                             Batch<T, A> no) noexcept {
    Batch<T, A> out;
    for (std::size_t i = 0; i < out.size; ++i)
        out.set_lane(i, m.lane(i) ? yes.lane(i) : no.lane(i));
    return out;
}
} // namespace lutils::simd
