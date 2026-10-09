#include <cmath>
#include <iostream>
#include <limits>
#include <lutils/compute/Pixel.hpp>
#include <stdexcept>
using lutils::compute::kernel::half;
void check(bool v) {
    if (!v)
        throw std::runtime_error("binary16 conversion mismatch");
}
int main() {
    try {
        for (std::uint32_t bits = 0; bits < 65536; ++bits) {
            auto value = half::fromBits(static_cast<std::uint16_t>(bits));
            float f = static_cast<float>(value);
            if ((bits & 0x7c00u) == 0x7c00u && (bits & 1023u))
                check(std::isnan(f));
            else
                check(half(f).bits() == bits);
        }
        for (std::uint16_t bits = 0; bits < 0x7bffu; ++bits) {
            double a = static_cast<double>(half::fromBits(bits));
            double b = static_cast<double>(half::fromBits(static_cast<std::uint16_t>(bits + 1)));
            double mid = (a + b) * 0.5;
            check(half(mid).bits() == ((bits & 1u) ? bits + 1u : bits));
            check(half(std::nextafter(mid, a)).bits() == bits);
            check(half(std::nextafter(mid, b)).bits() == bits + 1u);
            check(half(-mid).bits() == (half(mid).bits() | 0x8000u));
        }
        check(half(65519.0).bits() == 0x7bffu);
        check(half(65520.0).bits() == 0x7c00u);
        check(half(-0.0).bits() == 0x8000u);
        check(half(std::numeric_limits<double>::quiet_NaN()).bits() & 1023u);
        check(half(1.5f) + half(2.0f) == half(3.5f));
        static_assert(!std::is_constructible_v<half, long double>);
        using lutils::compute::kernel::ChannelConverter;
        check(ChannelConverter<std::uint16_t, half>::apply(UINT16_MAX) == half(1));
        check(ChannelConverter<std::uint32_t, half>::apply(UINT32_MAX) == half(1));
        check(ChannelConverter<std::int32_t, half>::apply(INT32_MAX) == half(1));
        check(ChannelConverter<std::int32_t, half>::apply(INT32_MIN) == half(-1));
        check(ChannelConverter<std::uint32_t, half>::apply(0) == half(0));
        check(ChannelConverter<std::uint16_t, half>::apply(32768) == half(0.5));
        std::cout << "all binary16 encodings and finite adjacent rounding boundaries passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
