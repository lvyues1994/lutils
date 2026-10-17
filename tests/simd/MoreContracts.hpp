#pragma once
// Included inside simd_test after the shared oracle helpers.
template <class Small, class Large, class A> void widePair() {
    using V = s::Batch<Small, A>;
    std::uint64_t state = 93721;
    for (int trial = 0; trial < 40; ++trial) {
        V input;
        for (std::size_t i = 0; i < V::size; ++i)
            input.set_lane(i, fromBits<Small>(next(state)));
        auto wide = s::widen<Large>(input);
        using W = typename decltype(wide)::batch_type;
        for (std::size_t i = 0; i < V::size; ++i)
            check(wide.parts[i / W::size].lane(i % W::size) == input.lane(i));
        check(s::all(s::narrow_saturate<Small>(wide) == input));
        check(s::all(s::narrow_truncate<Small>(wide) == input));
        for (auto edge :
             {std::numeric_limits<Large>::lowest(), std::numeric_limits<Large>::max()}) {
            for (auto &part : wide.parts)
                part = W{edge};
            check(s::all(s::narrow_saturate<Small>(wide) ==
                         V{edge == std::numeric_limits<Large>::max()
                               ? std::numeric_limits<Small>::max()
                               : std::numeric_limits<Small>::lowest()}));
        }
    }
}
template <class To, class From, class A> void roundingModes() {
    using F = s::Batch<From, A>;
    From const values[]{From{-2.5}, From{-1.5}, From{-0.5}, From{0.5},
                        From{1.5},  From{2.5},  From{1.75}, From{-1.75}};
    int const expected[][8]{{-2, -1, 0, 0, 1, 2, 1, -1},
                            {-2, -2, 0, 0, 2, 2, 2, -2},
                            {-3, -2, -1, 0, 1, 2, 1, -2},
                            {-2, -1, 0, 1, 2, 3, 2, -1}};
    s::Rounding const modes[]{s::Rounding::TowardZero, s::Rounding::NearestEven,
                              s::Rounding::Downward, s::Rounding::Upward};
    for (std::size_t m = 0; m < 4; ++m)
        for (std::size_t base = 0; base < 8; ++base) {
            F input;
            for (std::size_t i = 0; i < F::size; ++i)
                input.set_lane(i, values[(base + i) % 8]);
            auto result = s::convert_saturate<To>(input, modes[m]);
            for (std::size_t i = 0; i < F::size; ++i) {
                auto e = expected[m][(base + i) % 8];
                if constexpr (std::is_unsigned_v<To>)
                    e = std::max(e, 0);
                check(result.lane(i) == static_cast<To>(e));
            }
        }
    for (auto mode : modes) {
        check(s::all(s::convert_saturate<To>(F{std::numeric_limits<From>::infinity()}, mode) ==
                     s::Batch<To, A>{std::numeric_limits<To>::max()}));
        check(s::all(s::convert_saturate<To>(F{-std::numeric_limits<From>::infinity()}, mode) ==
                     s::Batch<To, A>{std::numeric_limits<To>::lowest()}));
        check(s::all(s::convert_saturate<To>(F{std::numeric_limits<From>::quiet_NaN()}, mode) ==
                     s::Batch<To, A>{}));
    }
    // The representable value immediately below 2^digits is inside the integer range.
    auto bound = std::ldexp(From{1}, std::numeric_limits<To>::digits);
    auto inside = std::nextafter(bound, From{});
    check(s::all(s::convert_saturate<To>(F{inside}, s::Rounding::TowardZero) ==
                 s::Batch<To, A>{static_cast<To>(inside)}));
}
template <class T, class A> void moreFloating() {
    using V = s::Batch<T, A>;
    T const data[]{T{0},
                   -T{0},
                   T{3},
                   T{-7},
                   std::numeric_limits<T>::infinity(),
                   std::numeric_limits<T>::quiet_NaN(),
                   std::numeric_limits<T>::denorm_min()};
    auto equal = [](T x, T y) {
        return (std::isnan(x) && std::isnan(y)) ||
               (x == y && (x != T{} || std::signbit(x) == std::signbit(y)));
    };
    for (std::size_t offset = 0; offset < 7; ++offset) {
        V a, b;
        for (std::size_t i = 0; i < V::size; ++i) {
            a.set_lane(i, data[(offset + i) % 7]);
            b.set_lane(i, data[(offset + i + 2) % 7]);
        }
        auto difference = a - b, quotient = a / b;
        for (std::size_t i = 0; i < V::size; ++i) {
            check(equal(difference.lane(i), a.lane(i) - b.lane(i)));
            check(equal(quotient.lane(i), a.lane(i) / b.lane(i)));
        }
        T expected{};
        for (std::size_t i = 0; i < V::size; ++i)
            expected += a.lane(i);
        check(equal(s::reduce_sum(a), expected));
    }
}
template <class V, std::size_t... I> V reverseShuffle(V v, std::index_sequence<I...>) {
    return s::shuffle<(V::size - 1 - I)...>(v);
}
template <class Integer, class Floating, class A> void integerToFloat() {
    using V = s::Batch<Integer, A>;
    std::uint64_t state = 71237;
    for (int trial = 0; trial < 40; ++trial) {
        V input;
        for (std::size_t i = 0; i < V::size; ++i)
            input.set_lane(i, fromBits<Integer>(next(state)));
        auto result = s::convert<Floating>(input);
        for (std::size_t i = 0; i < V::size; ++i)
            check(result.lane(i) == static_cast<Floating>(input.lane(i)));
    }
    Integer const edges[]{std::numeric_limits<Integer>::lowest(),
                           std::numeric_limits<Integer>::max(), Integer{0}, Integer{1},
                           Integer{16777215}, Integer{16777216}, Integer{16777217}};
    for (std::size_t base = 0; base < 7; ++base) {
        V input;
        for (std::size_t i = 0; i < V::size; ++i)
            input.set_lane(i, edges[(base + i) % 7]);
        auto out = s::convert<Floating>(input);
        for (std::size_t i = 0; i < V::size; ++i)
            check(out.lane(i) == static_cast<Floating>(input.lane(i)));
        if constexpr (std::is_signed_v<Integer> && std::is_same_v<Floating, float>) {
            input = V{-16777217};
            check(s::all(s::convert<Floating>(input) == s::Batch<Floating, A>{Floating{-16777216}}));
        }
    }
}
template <class T, class A> void comparisonBits() {
    using V = s::Batch<T, A>;
    for (unsigned bits = 0; bits < (1u << V::size); ++bits) {
        V a, b{std::numeric_limits<T>::max()};
        for (std::size_t i = 0; i < V::size; ++i)
            a.set_lane(i, ((bits >> i) & 1) ? std::numeric_limits<T>::lowest()
                                            : std::numeric_limits<T>::max());
        auto less = a < b, equal = a == b;
        for (std::size_t i = 0; i < V::size; ++i) {
            check(less.lane(i) == bool((bits >> i) & 1));
            check(equal.lane(i) != less.lane(i));
        }
    }
}
template <class A> void moreContracts() {
    comparisonBits<std::int32_t, A>();
    comparisonBits<std::uint32_t, A>();
    comparisonBits<std::int64_t, A>();
    comparisonBits<std::uint64_t, A>();
    widePair<std::uint8_t, std::uint16_t, A>();
    widePair<std::uint8_t, std::uint32_t, A>();
    widePair<std::uint8_t, std::uint64_t, A>();
    widePair<std::uint16_t, std::uint32_t, A>();
    widePair<std::uint16_t, std::uint64_t, A>();
    widePair<std::uint32_t, std::uint64_t, A>();
    widePair<std::int8_t, std::int16_t, A>();
    widePair<std::int8_t, std::int32_t, A>();
    widePair<std::int8_t, std::int64_t, A>();
    widePair<std::int16_t, std::int32_t, A>();
    widePair<std::int16_t, std::int64_t, A>();
    widePair<std::int32_t, std::int64_t, A>();
    roundingModes<std::int32_t, float, A>();
    roundingModes<std::uint32_t, float, A>();
    roundingModes<std::int64_t, double, A>();
    roundingModes<std::uint64_t, double, A>();
    moreFloating<float, A>();
    moreFloating<double, A>();
    integerToFloat<std::int32_t, float, A>();
    integerToFloat<std::uint32_t, float, A>();
    integerToFloat<std::int64_t, double, A>();
    integerToFloat<std::uint64_t, double, A>();
    using F = s::Batch<float, A>;
    F fractional;
    for (std::size_t i = 0; i < F::size; ++i)
        fractional.set_lane(i, static_cast<float>(i) - 3.75f);
    auto doubles = s::widen<double>(fractional);
    using D = typename decltype(doubles)::batch_type;
    for (std::size_t i = 0; i < F::size; ++i)
        check(doubles.parts[i / D::size].lane(i % D::size) ==
              static_cast<double>(fractional.lane(i)));
    using V = s::Batch<std::int32_t, A>;
    for (std::size_t n = 1; n < 4 * V::size + 3; ++n) {
        std::vector<std::int32_t> input(n, 7);
        auto pred = [](V v) { return v == V{}; };
        check((!s::find_if<std::int32_t, A>(input.data(), n, pred)));
        for (std::size_t i = 0; i < n; ++i) {
            input[i] = 0;
            check((s::find_if<std::int32_t, A>(input.data(), n, pred) == i));
            input[i] = 7;
        }
    }
    using B = s::Batch<std::uint8_t, A>;
    B sequence;
    for (std::size_t i = 0; i < B::size; ++i)
        sequence.set_lane(i, static_cast<std::uint8_t>(i + 1));
    auto reverse = reverseShuffle(sequence, std::make_index_sequence<B::size>{});
    for (std::size_t i = 0; i < B::size; ++i)
        check(reverse.lane(i) == sequence.lane(B::size - 1 - i));
    std::uint64_t state = 74159;
    for (int trial = 0; trial < 2000; ++trial) {
        typename B::mask_type mask;
        auto bits = next(state);
        for (std::size_t i = 0; i < B::size; ++i)
            mask.set_lane(i, ((bits >> i) & 1) != 0);
        auto selected = s::select(mask, sequence, B{99});
        auto compressed = s::compress(sequence, mask);
        std::size_t j = 0;
        for (std::size_t i = 0; i < B::size; ++i) {
            check(selected.lane(i) == (mask.lane(i) ? sequence.lane(i) : 99));
            if (mask.lane(i))
                check(compressed.values.lane(j++) == sequence.lane(i));
        }
        check(j == compressed.count);
    }
    using I = s::Batch<std::int16_t, A>;
    for (unsigned amount = 0; amount <= 17; ++amount) {
        I input;
        for (std::size_t i = 0; i < I::size; ++i)
            input.set_lane(i, static_cast<std::int16_t>(int(i) * 3701 - 17003));
        auto shifted = s::shift_right_arithmetic(input, amount);
        for (std::size_t i = 0; i < I::size; ++i) {
            auto x = int(input.lane(i)), divisor = 1 << amount;
            auto expected = x / divisor - (x < 0 && x % divisor != 0 ? 1 : 0);
            check(shifted.lane(i) == expected);
        }
    }
    auto wide = s::widen<std::int16_t>(s::Batch<std::int8_t, A>{-1});
    check(s::all(s::narrow_saturate<std::uint8_t>(wide) == B{}));
    for (auto &part : wide.parts)
        part = I{255};
    check(s::all(s::narrow_saturate<std::int8_t>(wide) == s::Batch<std::int8_t, A>{127}));
    check(s::all(s::narrow_truncate<std::int8_t>(wide) == s::Batch<std::int8_t, A>{-1}));
}
