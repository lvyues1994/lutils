#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace lutils::compute::kernel {
static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<double>::is_iec559,
              "half requires IEEE binary32/binary64 host types");
// IEEE 754 binary16 storage with round-to-nearest, ties-to-even conversion.
// CPU arithmetic is rounded back to binary16 after every operation.
struct half {
    constexpr half() = default;
    template <class T,
              std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, long double>, int> = 0>
    explicit half(T value) : bits_(encode(static_cast<double>(value))) {}
    static constexpr half fromBits(std::uint16_t bits) {
        half result;
        result.bits_ = bits;
        return result;
    }
    constexpr std::uint16_t bits() const { return bits_; }
    explicit operator float() const {
        std::uint32_t sign = static_cast<std::uint32_t>(bits_ & 0x8000u) << 16;
        std::uint32_t exponent = (bits_ >> 10) & 31u, fraction = bits_ & 1023u;
        std::uint32_t out;
        if (!exponent) {
            if (!fraction)
                out = sign;
            else {
                int e = -14;
                while (!(fraction & 1024u)) {
                    fraction <<= 1;
                    --e;
                }
                out =
                    sign | (static_cast<std::uint32_t>(e + 127) << 23) | ((fraction & 1023u) << 13);
            }
        } else if (exponent == 31u)
            out = sign | 0x7f800000u | (fraction << 13);
        else
            out = sign | ((exponent + 112u) << 23) | (fraction << 13);
        float value;
        std::memcpy(&value, &out, sizeof(value));
        return value;
    }
    explicit operator double() const { return static_cast<double>(static_cast<float>(*this)); }
    half operator-() const { return fromBits(static_cast<std::uint16_t>(bits_ ^ 0x8000u)); }
    half operator+() const { return *this; }
#define LUTILS_HALF_OPERATOR(OP)                                                                   \
    friend half operator OP(half a, half b) {                                                      \
        return half(static_cast<double>(a) OP static_cast<double>(b));                             \
    }                                                                                              \
    half &operator OP##=(half b) { return *this = *this OP b; }
    LUTILS_HALF_OPERATOR(+)
    LUTILS_HALF_OPERATOR(-)
    LUTILS_HALF_OPERATOR(*)
    LUTILS_HALF_OPERATOR(/)
#undef LUTILS_HALF_OPERATOR
#define LUTILS_HALF_COMPARE(OP)                                                                    \
    friend bool operator OP(half a, half b) {                                                      \
        return static_cast<float>(a) OP static_cast<float>(b);                                     \
    }
    LUTILS_HALF_COMPARE(==)
    LUTILS_HALF_COMPARE(!=)
    LUTILS_HALF_COMPARE(<)
    LUTILS_HALF_COMPARE(<=)
    LUTILS_HALF_COMPARE(>)
    LUTILS_HALF_COMPARE(>=)
#undef LUTILS_HALF_COMPARE
  private:
    std::uint16_t bits_ = 0;
    static std::uint64_t rounded(std::uint64_t value, unsigned shift) {
        auto q = value >> shift;
        auto remainder = value & ((std::uint64_t{1} << shift) - 1);
        auto midpoint = std::uint64_t{1} << (shift - 1);
        return q + (remainder > midpoint || (remainder == midpoint && (q & 1u)));
    }
    static std::uint16_t encode(double value) {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        auto sign = static_cast<std::uint16_t>((bits >> 48) & 0x8000u);
        auto exponent = static_cast<unsigned>((bits >> 52) & 2047u);
        auto fraction = bits & 0x000fffffffffffffull;
        if (exponent == 2047u)
            return static_cast<std::uint16_t>(sign | 0x7c00u |
                                              (fraction ? ((fraction >> 42) | 0x200u) : 0u));
        int e = static_cast<int>(exponent) - 1023;
        if (e > 15)
            return static_cast<std::uint16_t>(sign | 0x7c00u);
        if (e < -25)
            return sign;
        auto mantissa = fraction | (std::uint64_t{1} << 52);
        if (e < -14)
            return static_cast<std::uint16_t>(sign |
                                              rounded(mantissa, static_cast<unsigned>(28 - e)));
        auto encoded = static_cast<std::uint64_t>(e + 14) * 1024u + rounded(mantissa, 42);
        return static_cast<std::uint16_t>(sign | encoded);
    }
};
static_assert(sizeof(half) == 2 && std::is_trivially_copyable_v<half>);
template <class T>
inline constexpr bool isShaderFloat = std::is_floating_point_v<T> || std::is_same_v<T, half>;
} // namespace lutils::compute::kernel
