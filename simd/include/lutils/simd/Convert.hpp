#pragma once
#include <cmath>
#include <lutils/simd/Batch.hpp>
#include <lutils/simd/detail/Conversions.hpp>

namespace lutils::simd {
template <class V, std::size_t N> struct WideParts {
    using batch_type = V;
    static constexpr std::size_t size = N;
    std::array<V, N> parts;
};
template <class To, class From, class A>
LUTILS_SIMD_INLINE auto widen(Batch<From, A> value) noexcept {
    static_assert(sizeof(To) > sizeof(From), "widen requires a larger element type");
    static_assert((std::is_integral_v<To> && std::is_integral_v<From> &&
                   std::is_signed_v<To> == std::is_signed_v<From>) ||
                      (std::is_floating_point_v<To> && std::is_floating_point_v<From>),
                  "widen preserves the numeric category and signedness");
    using V = Batch<To, A>;
    constexpr auto parts = value.size / V::size;
    WideParts<V, parts> out{};
    if constexpr (parts == 2 && detail::WidenNative<To, From, A>::available) {
        detail::WidenNative<To, From, A>::run(detail::Access::data(out.parts[0]),
                                              detail::Access::data(out.parts[1]),
                                              detail::Access::data(value));
        return out;
    }
    for (std::size_t i = 0; i < value.size; ++i)
        out.parts[i / V::size].set_lane(i % V::size, static_cast<To>(value.lane(i)));
    return out;
}
namespace detail {
template <class To, class From> LUTILS_SIMD_INLINE To saturateInteger(From value) noexcept {
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    constexpr auto low = std::numeric_limits<To>::lowest(), high = std::numeric_limits<To>::max();
    if constexpr (std::is_signed_v<From>) {
        auto x = static_cast<std::int64_t>(value);
        if (x < 0) {
            if constexpr (std::is_unsigned_v<To>)
                return To{};
            else if (x < static_cast<std::int64_t>(low))
                return low;
        } else if (static_cast<std::uint64_t>(x) > static_cast<std::uint64_t>(high))
            return high;
    } else {
        if (static_cast<std::uint64_t>(value) > static_cast<std::uint64_t>(high))
            return high;
    }
    return static_cast<To>(value);
}
} // namespace detail
template <class To, class From, class A, std::size_t N>
LUTILS_SIMD_INLINE Batch<To, A> narrow_saturate(WideParts<Batch<From, A>, N> const &wide) noexcept {
    static_assert(sizeof(To) < sizeof(From), "narrow requires a smaller element type");
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    Batch<To, A> out;
    static_assert(N * Batch<From, A>::size == out.size, "narrow must supply every output lane");
    if constexpr (N == 2 && detail::NarrowNative<To, From, A>::available) {
        detail::NarrowNative<To, From, A>::run(detail::Access::data(out),
                                               detail::Access::data(wide.parts[0]),
                                               detail::Access::data(wide.parts[1]));
        return out;
    }
    for (std::size_t i = 0; i < out.size; ++i)
        out.set_lane(i, detail::saturateInteger<To>(
                            wide.parts[i / Batch<From, A>::size].lane(i % Batch<From, A>::size)));
    return out;
}
template <class To, class From, class A, std::size_t N>
LUTILS_SIMD_INLINE Batch<To, A> narrow_truncate(WideParts<Batch<From, A>, N> const &wide) noexcept {
    static_assert(sizeof(To) < sizeof(From), "narrow requires a smaller element type");
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    Batch<To, A> out;
    static_assert(N * Batch<From, A>::size == out.size, "narrow must supply every output lane");
    for (std::size_t i = 0; i < out.size; ++i)
        out.set_lane(i, detail::wrap<To>(static_cast<std::uint64_t>(
                            wide.parts[i / Batch<From, A>::size].lane(i % Batch<From, A>::size))));
    return out;
}
enum class Rounding { TowardZero, NearestEven, Downward, Upward };
template <class To, class From, class A,
          std::enable_if_t<std::is_integral_v<To> && std::is_floating_point_v<From>, int> = 0>
LUTILS_SIMD_INLINE Batch<To, A> convert_saturate(Batch<From, A> value, Rounding rounding) noexcept {
    Batch<To, A> out;
    static_assert(out.size == value.size,
                  "convert requires equal lane counts; widen/narrow separately");
    constexpr auto low = std::numeric_limits<To>::lowest(), high = std::numeric_limits<To>::max();
    for (std::size_t i = 0; i < value.size; ++i) {
        auto x = static_cast<long double>(value.lane(i));
        if (x != x) {
            out.set_lane(i, To{});
            continue;
        }
        if (x <= static_cast<long double>(low)) {
            out.set_lane(i, low);
            continue;
        }
        if (x >= static_cast<long double>(high)) {
            out.set_lane(i, high);
            continue;
        }
        switch (rounding) {
        case Rounding::TowardZero:
            x = std::trunc(x);
            break;
        case Rounding::Downward:
            x = std::floor(x);
            break;
        case Rounding::Upward:
            x = std::ceil(x);
            break;
        case Rounding::NearestEven: {
            auto base = std::floor(x), fraction = x - base;
            x = base +
                ((fraction > 0.5L || (fraction == 0.5L && std::fmod(base, 2.0L) != 0)) ? 1 : 0);
            break;
        }
        }
        if (x <= static_cast<long double>(low))
            out.set_lane(i, low);
        else if (x >= static_cast<long double>(high))
            out.set_lane(i, high);
        else
            out.set_lane(i, static_cast<To>(x));
    }
    return out;
}
template <class To, class From, class A,
          std::enable_if_t<std::is_floating_point_v<To> && std::is_integral_v<From>, int> = 0>
LUTILS_SIMD_INLINE Batch<To, A> convert(Batch<From, A> value) noexcept {
    Batch<To, A> out;
    static_assert(out.size == value.size, "convert requires equal lane counts");
    if constexpr (detail::ConvertNative<To, From, A>::available) {
        detail::ConvertNative<To, From, A>::run(detail::Access::data(out), detail::Access::data(value));
        return out;
    }
    for (std::size_t i = 0; i < value.size; ++i)
        out.set_lane(i, static_cast<To>(value.lane(i)));
    return out;
}
template <class To, class From, class A>
LUTILS_SIMD_INLINE Batch<To, A> bit_cast(Batch<From, A> value) noexcept {
    Batch<To, A> out;
    static_assert(sizeof(To) * out.size == sizeof(From) * value.size,
                  "bit_cast requires equal byte counts");
    std::memcpy(detail::Access::data(out), detail::Access::data(value), sizeof(To) * out.size);
    return out;
}
} // namespace lutils::simd
