#pragma once
#include <array>
#include <cassert>
#include <lutils/simd/detail/Scalar.hpp>

namespace lutils::simd {
template <class T, class Abi> class Mask;
namespace detail {
struct Access;
}

template <class T, class Abi> class alignas(Abi::bytes ? Abi::bytes : alignof(T)) Batch {
    static_assert(detail::element<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "SIMD requires an unqualified fixed-width integer, float or double");

  public:
    using value_type = T;
    using abi_type = Abi;
    using mask_type = Mask<T, Abi>;
    static constexpr std::size_t size = detail::lanes<T, Abi>;
    static constexpr std::size_t alignment = Abi::bytes ? Abi::bytes : alignof(T);
    LUTILS_SIMD_INLINE constexpr Batch() noexcept = default;
    explicit LUTILS_SIMD_INLINE constexpr Batch(T value) noexcept {
        for (auto &lane : values_)
            lane = value;
    }
    LUTILS_SIMD_INLINE T lane(std::size_t index) const noexcept {
        assert(index < size);
        return values_[index];
    }
    LUTILS_SIMD_INLINE void set_lane(std::size_t index, T value) noexcept {
        assert(index < size);
        values_[index] = value;
    }

  private:
    std::array<T, size> values_{};
    friend struct detail::Access;
};
namespace detail {
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
} // namespace lutils::simd
