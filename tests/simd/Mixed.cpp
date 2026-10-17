#include "Mixed.hpp"
#include <lutils/simd.hpp>
namespace s = lutils::simd;
#if defined(LUTILS_MIXED_ADVANCED)
#define ADDRESS advancedAddress
#define ARITHMETIC advancedArithmetic
#define PACKING advancedPacking
#else
#define ADDRESS baselineAddress
#define ARITHMETIC baselineArithmetic
#define PACKING baselinePacking
#endif
namespace {
std::optional<std::size_t> mismatchWrapper(std::uint8_t const *a, std::uint8_t const *b,
                                           std::size_t n) noexcept {
    return s::mismatch<std::uint8_t, s::abi::sse2>(a, b, n);
}
} // namespace
MismatchFunction ADDRESS() { return mismatchWrapper; }
void ARITHMETIC(float *out, float const *a, float const *b, float const *c) {
    using V = s::Batch<float, s::abi::sse2>;
    s::store(out, s::load<V>(a) * s::load<V>(b) + s::load<V>(c));
}
#undef ADDRESS
void PACKING(std::uint8_t *out, std::uint8_t const *in) {
    using V = s::Batch<std::uint8_t, s::abi::sse2>;
    auto wide = s::widen<std::uint16_t>(s::load<V>(in));
    s::WideParts<s::Batch<std::int16_t, s::abi::sse2>, 2> shifted;
    for (std::size_t i = 0; i < 2; ++i)
        shifted.parts[i] = s::shift_right_arithmetic(s::bit_cast<std::int16_t>(wide.parts[i]), 1);
    s::store(out, s::narrow_saturate<std::uint8_t>(shifted));
}
#undef PACKING
#undef ARITHMETIC
