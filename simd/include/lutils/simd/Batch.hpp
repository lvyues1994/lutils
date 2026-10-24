#pragma once
#include <array>
#include <cassert>
#include <lutils/simd/detail/Scalar.hpp>

namespace lutils::simd {
template <class T, class Abi> class Mask;
namespace detail {
struct Access;
}

template <class T, class Abi> class alignas(detail::Shape<T, Abi>::alignment) Batch {
    static_assert(detail::element<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "SIMD requires an unqualified fixed-width integer, float or double");

  public:
    using value_type = T;
    using abi_type = Abi;
    using mask_type = Mask<T, Abi>;
    static constexpr std::size_t size = detail::lanes<T, Abi>;
    static constexpr std::size_t alignment = detail::Shape<T, Abi>::alignment;
    LUTILS_SIMD_INLINE constexpr Batch() noexcept = default;
    explicit LUTILS_SIMD_INLINE constexpr Batch(T value) noexcept {
        for (auto &lane : values_)
            lane = value;
    }
    LUTILS_SIMD_INLINE constexpr T lane(std::size_t index) const noexcept {
        assert(index < size);
        return values_[index];
    }
    LUTILS_SIMD_INLINE constexpr void set_lane(std::size_t index, T value) noexcept {
        assert(index < size);
        values_[index] = value;
    }

  private:
    std::array<T, size> values_{};
    friend struct detail::Access;
};
template <class T, std::size_t N, class A = abi::scalar> using Vec = Batch<T, abi::fixed<N, A>>;
namespace detail {
template <class T, std::size_t N, class A>
using Resized = Batch<T, std::conditional_t<N == lanes<T, typename Shape<T, A>::native_abi>,
                                            typename Shape<T, A>::native_abi,
                                            abi::fixed<N, typename Shape<T, A>::native_abi>>>;
template <class To, class V> struct Rebind;
template <class To, class From, class A> struct Rebind<To, Batch<From, A>> {
    using type = Resized<To, Batch<From, A>::size, A>;
};
struct Access {
    template <class T, class A> static LUTILS_SIMD_INLINE T *data(Batch<T, A> &v) noexcept {
        return v.values_.data();
    }
    template <class T, class A>
    static LUTILS_SIMD_INLINE T const *data(Batch<T, A> const &v) noexcept {
        return v.values_.data();
    }
};
} // namespace detail
template <class To, class V> using rebind_t = typename detail::Rebind<To, V>::type;
} // namespace lutils::simd
