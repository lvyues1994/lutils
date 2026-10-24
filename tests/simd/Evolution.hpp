#pragma once
// Included inside simd_test; uses the independent scalar oracle helpers.
template <std::size_t N> constexpr std::size_t lastLane(std::size_t i) { return N - 1 - i; }
template <std::size_t N> constexpr std::size_t inputBoundary(std::size_t i) { return N - 1 + i; }
template <class T, class A> constexpr bool constexprMask() {
    using V = s::Batch<T, A>;
    auto empty = s::first_n<V>(0), full = s::first_n<V>(V::size);
    if (!s::none(empty) || !s::all(full) || s::count(full) != V::size)
        return false;
    empty.set_lane(V::size - 1, true);
    auto found = s::first_true(empty);
    return found && *found == V::size - 1 && s::count(empty) == 1 && s::all(empty | ~empty) &&
           s::none(empty ^ empty) && s::count(full & empty) == 1;
}
template <class T, class A> void composedMask() {
    using V = s::Batch<T, A>;
    static_assert(constexprMask<T, A>());
    std::uint64_t state = 14537;
    for (unsigned trial = 0; trial < 64; ++trial) {
        V yes, no, a, b;
        typename V::mask_type alternate;
        for (std::size_t i = 0; i < V::size; ++i) {
            auto x = static_cast<s::detail::MaskLane<T>>(next(state));
            auto y = static_cast<s::detail::MaskLane<T>>(next(state));
            T xx, yy;
            std::memcpy(&xx, &x, sizeof(T));
            std::memcpy(&yy, &y, sizeof(T));
            yes.set_lane(i, xx);
            no.set_lane(i, yy);
            a.set_lane(i, static_cast<T>(i % 13));
            b.set_lane(i, static_cast<T>(trial % 13));
            alternate.set_lane(i, (next(state) & 1) != 0);
        }
        auto m = ((a < b) | alternate) & ~(a == b);
        auto selected = s::select(m, yes, no);
        auto compact = s::detail::MaskAccess::bits(m);
        auto restored = s::detail::MaskAccess::from<T, A>(compact);
        std::size_t expectedCount = 0;
        for (std::size_t i = 0; i < V::size; ++i) {
            auto chosen = (a.lane(i) < b.lane(i) || alternate.lane(i)) && a.lane(i) != b.lane(i);
            check(m.lane(i) == chosen && restored.lane(i) == chosen);
            auto x = selected.lane(i), y = chosen ? yes.lane(i) : no.lane(i);
            check(std::memcmp(&x, &y, sizeof(T)) == 0);
            expectedCount += chosen;
        }
        check(s::count(m) == expectedCount);
        if constexpr (std::is_integral_v<T>) {
            auto low = s::min(yes, no), high = s::max(yes, no);
            std::uint64_t sum = 0;
            T minimum = yes.lane(0), maximum = minimum;
            for (std::size_t i = 0; i < V::size; ++i) {
                check(low.lane(i) == std::min(yes.lane(i), no.lane(i)));
                check(high.lane(i) == std::max(yes.lane(i), no.lane(i)));
                sum += static_cast<std::make_unsigned_t<T>>(yes.lane(i));
                minimum = std::min(minimum, yes.lane(i));
                maximum = std::max(maximum, yes.lane(i));
            }
            check(s::reduce_sum(yes) == fromBits<T>(sum));
            check(s::reduce_min(yes) == minimum && s::reduce_max(yes) == maximum);
        }
        auto zipped = s::interleave(yes, no);
        auto unzipped = s::deinterleave(zipped.low, zipped.high);
        auto last = s::permute<1, lastLane<V::size>>(yes);
        auto actualLast = last.lane(0), expectedLast = yes.lane(V::size - 1);
        check(std::memcmp(&actualLast, &expectedLast, sizeof(T)) == 0);
        if constexpr (V::size >= 2) {
            auto pair = s::permute<2, lastLane<V::size>>(yes);
            auto boundary = s::permute<2, inputBoundary<V::size>>(yes, no);
            for (std::size_t i = 0; i < 2; ++i) {
                auto x = pair.lane(i), y = yes.lane(V::size - 1 - i);
                auto z = boundary.lane(i), w = i ? no.lane(0) : yes.lane(V::size - 1);
                check(std::memcmp(&x, &y, sizeof(T)) == 0);
                check(std::memcmp(&z, &w, sizeof(T)) == 0);
            }
        }
        for (std::size_t i = 0; i < V::size; ++i) {
            auto x = yes.lane(i), y = unzipped.even.lane(i);
            auto z = no.lane(i), w = unzipped.odd.lane(i);
            check(std::memcmp(&x, &y, sizeof(T)) == 0 && std::memcmp(&z, &w, sizeof(T)) == 0);
        }
    }
}
template <class V> void checkedMemory() {
    using T = typename V::value_type;
    std::array<T, V::size + 2> input{}, output{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<T>(i % 100);
    V transferred;
    s::load(input.data() + 1, transferred);
    for (std::size_t i = 0; i < V::size; ++i)
        check(transferred.lane(i) == input[i + 1]);
    // Reference load/store must also preserve a self-transfer.
    s::load(s::detail::Access::data(transferred), transferred);
    s::store(s::detail::Access::data(transferred), transferred);
    for (std::size_t i = 0; i < V::size; ++i)
        check(transferred.lane(i) == input[i + 1]);
    if constexpr (V::size > 1) {
        s::store_partial(s::detail::Access::data(transferred) + 1, transferred, V::size - 1);
        for (std::size_t i = 1; i < V::size; ++i)
            check(transferred.lane(i) == input[i]);
        s::load(s::detail::Access::data(transferred), transferred);
    }
    for (std::size_t n = 0; n <= V::size; ++n) {
        s::MemoryView<T const> source(n ? input.data() + 1 : nullptr, n);
        output.fill(T{77});
        s::MemoryView<T> destination(output.data() + 1, n);
        auto loaded = s::load_partial<V>(source, 0, T{99});
        check(loaded.count == n && s::count(loaded.valid) == n);
        check(bool(s::try_load<V>(source)) == (n == V::size));
        for (std::size_t i = 0; i < V::size; ++i)
            check(loaded.value.lane(i) == (i < n ? input[i + 1] : T{99}));
        check(s::try_store(destination, loaded.value) == (n == V::size));
        if (n < V::size)
            check(std::all_of(output.begin(), output.end(), [](T x) { return x == T{77}; }));
        check(s::store_partial(destination, loaded.value) == n);
        for (std::size_t i = 0; i < output.size(); ++i)
            check(output[i] == (i > 0 && i <= n ? input[i] : T{77}));
        check(s::load_partial<V>(source, n + 1).count == 0);
        check(!s::try_load<V>(source, std::numeric_limits<std::size_t>::max()));
        check(!s::try_store(destination, loaded.value, std::numeric_limits<std::size_t>::max()));
        check(s::store_partial(destination, loaded.value, n + 1) == 0);
    }
    check(s::store_partial(s::MemoryView<T>{nullptr, 0}, V{}) == 0);
    check(!s::try_store(s::MemoryView<T>{nullptr, 0}, V{}));
}
constexpr std::size_t everyOther(std::size_t i) { return 2 * i; }
constexpr std::size_t constantOnlyIndex(std::size_t) {
    return s::detail::constantEvaluated() ? 0 : 1000;
}
template <std::size_t N> constexpr std::size_t backwards(std::size_t i) { return N - 1 - i; }
template <std::size_t N, class A> void fixedWidth() {
    using V = s::Vec<std::int32_t, N, A>;
    V a, b;
    for (std::size_t i = 0; i < N; ++i) {
        a.set_lane(i, fromBits<std::int32_t>(0x7fffffffULL - i));
        b.set_lane(i, static_cast<std::int32_t>(i + 1));
    }
    auto sum = a + b, low = s::min(a, b), high = s::max(a, b);
    std::uint32_t expected = 0;
    for (std::size_t i = 0; i < N; ++i) {
        check(sum.lane(i) == std::numeric_limits<std::int32_t>::lowest());
        check(low.lane(i) == b.lane(i) && high.lane(i) == a.lane(i));
        expected += static_cast<std::uint32_t>(a.lane(i));
    }
    check(s::reduce_sum(a) == fromBits<std::int32_t>(expected));
    check(s::reduce_min(b) == 1 && s::reduce_max(b) == static_cast<std::int32_t>(N));
    auto reversed = s::permute<N, backwards<N>>(b);
    auto constantIndexes = s::permute<N, constantOnlyIndex>(b);
    for (std::size_t i = 0; i < N; ++i)
        check(reversed.lane(i) == b.lane(N - 1 - i) && constantIndexes.lane(i) == b.lane(0));
    auto doubles = s::convert<double>(b);
    static_assert(decltype(doubles)::size == N);
    static_assert(s::rebind_t<std::uint8_t, V>::size == N);
    auto restored = s::convert_saturate<std::int32_t>(doubles, s::Rounding::NearestEven);
    for (std::size_t i = 0; i < N; ++i)
        check(restored.lane(i) == b.lane(i));
    auto wide = s::widen<std::int64_t>(b);
    check(s::all(s::narrow_truncate<std::int32_t>(wide) == b));
    if constexpr (N <= 16) {
        auto bytes = s::bit_cast<std::uint8_t>(b);
        static_assert(decltype(bytes)::size == 4 * N);
        auto back = s::bit_cast<std::int32_t>(bytes);
        for (std::size_t i = 0; i < N; ++i)
            check(back.lane(i) == b.lane(i));
    }
    if constexpr (N <= 32) {
        auto joined = s::concat(a, b);
        auto parts = s::split<N>(joined);
        for (std::size_t i = 0; i < N; ++i)
            check(parts.low.lane(i) == a.lane(i) && parts.high.lane(i) == b.lane(i));
        auto even = s::permute<N, everyOther>(a, b);
        for (std::size_t i = 0; i < N; ++i)
            check(even.lane(i) == (2 * i < N ? a.lane(2 * i) : b.lane(2 * i - N)));
    }
    composedMask<float, s::abi::fixed<N, A>>();
    floating<float, s::abi::fixed<N, A>>();
    checkedMemory<V>();
    using F = s::Vec<float, N, A>;
    volatile float runtime = 6.0f;
    F numerator{runtime}, denominator{runtime / 3.0f};
    std::feclearexcept(FE_ALL_EXCEPT);
    auto quotient = numerator / denominator;
    std::array<float, N> result{};
    s::store(result.data(), quotient);
    check((std::fetestexcept(FE_INVALID | FE_DIVBYZERO) == 0));
    for (auto x : result)
        check(x == 3.0f);
}
template <class A> void evolutionContracts() {
    composedMask<std::int8_t, A>();
    composedMask<std::uint8_t, A>();
    composedMask<std::int16_t, A>();
    composedMask<std::uint16_t, A>();
    composedMask<std::int32_t, A>();
    composedMask<std::uint32_t, A>();
    composedMask<std::int64_t, A>();
    composedMask<std::uint64_t, A>();
    composedMask<float, A>();
    composedMask<double, A>();
    checkedMemory<s::Batch<std::uint8_t, A>>();
    checkedMemory<s::Batch<double, A>>();
    composedMask<std::uint8_t, s::abi::fixed<64, A>>();
    fixedWidth<1, A>();
    fixedWidth<3, A>();
    fixedWidth<7, A>();
    fixedWidth<8, A>();
    fixedWidth<9, A>();
    fixedWidth<19, A>();
    fixedWidth<64, A>();
}
