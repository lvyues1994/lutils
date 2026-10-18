#pragma once
#include "Extended.hpp"
#include <lutils/simd.hpp>
namespace extended {
namespace s = lutils::simd;
template <class T, class A> std::uint64_t convert(Data const &d) {
    using F = s::Batch<float, A>;
    auto *out = static_cast<T *>(d.output);
    auto const *in = static_cast<float const *>(d.input);
    auto rounding = static_cast<s::Rounding>(d.rounding);
    std::size_t i = 0;
    for (; d.size - i >= F::size; i += F::size)
        s::store(out + i, s::convert_saturate<T>(s::load<F>(in + i), rounding));
    if (i < d.size)
        s::store_partial(out + i,
                         s::convert_saturate<T>(s::load_partial<F>(in + i, d.size - i), rounding),
                         d.size - i);
    return d.size;
}
// Keep policy dispatch outside the measured kernel body in both revisions.
template <bool Overwrite, class T, class A>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
std::uint64_t
filterBody(Data const &d) {
    using V = s::Batch<T, A>;
#if __has_include(<lutils/simd/FilterWrite.hpp>)
    if constexpr (Overwrite)
        return s::copy_if<T, A>(
            static_cast<T const *>(d.input), d.size, static_cast<T *>(d.output),
            [](V v) { return v > V{T{100}}; }, s::FilterWrite::AllowOverwrite);
#endif
    // Older revisions satisfy the relaxed contract with their exact-write path.
    return s::copy_if<T, A>(static_cast<T const *>(d.input), d.size, static_cast<T *>(d.output),
                            [](V v) { return v > V{T{100}}; });
}
template <class T, class A> std::uint64_t filter(Data const &d) {
    return d.allowOverwrite ? filterBody<true, T, A>(d) : filterBody<false, T, A>(d);
}
template <class A> std::uint64_t execute(Operation op, Data const &d) {
    switch (op) {
    case Operation::Signed:
        return convert<std::int32_t, A>(d);
    case Operation::Unsigned:
        return convert<std::uint32_t, A>(d);
    case Operation::Filter8:
        return filter<std::uint8_t, A>(d);
    case Operation::Filter16:
        return filter<std::uint16_t, A>(d);
    case Operation::Filter32:
        return filter<std::uint32_t, A>(d);
    case Operation::Filter64:
        return filter<std::uint64_t, A>(d);
    }
    return 0;
}
} // namespace extended
