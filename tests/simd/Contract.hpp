#pragma once
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <limits>
#include <lutils/simd.hpp>
#include <memory>
#include <stdexcept>
#include <vector>
#if defined(__unix__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace simd_test {
namespace s = lutils::simd;
inline void check(bool condition) {
    if (!condition)
        throw std::runtime_error("SIMD contract mismatch");
}
template <class T> T fromBits(std::uint64_t value) {
    auto u = static_cast<std::make_unsigned_t<T>>(value);
    T out;
    std::memcpy(&out, &u, sizeof(T));
    return out;
}
inline std::uint64_t next(std::uint64_t &state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}
template <class T, class A> void integers() {
    using V = s::Batch<T, A>;
    using U = std::make_unsigned_t<T>;
    std::uint64_t state = 0x123456789abcdef;
    std::array<T, V::size> aa{}, bb{};
    for (unsigned iteration = 0; iteration < 300; ++iteration) {
        for (std::size_t i = 0; i < V::size; ++i) {
            aa[i] = fromBits<T>(next(state));
            bb[i] = fromBits<T>(next(state));
        }
        if (iteration < 5) {
            T const edges[]{T{}, T{1}, std::numeric_limits<T>::lowest(),
                            std::numeric_limits<T>::max(), fromBits<T>(~std::uint64_t{0})};
            for (std::size_t i = 0; i < V::size; ++i) {
                aa[i] = edges[iteration];
                bb[i] = edges[i % 5];
            }
        }
        auto a = s::load<V>(aa.data()), b = s::load<V>(bb.data());
        auto sum = a + b, difference = a - b, product = a * b;
        auto eq = a == b, lt = a<b, le = a <= b, ge = a >= b, gt = a> b;
        auto anded = a & b, ored = a | b, xored = a ^ b;
        auto saturated = s::add_saturate(a, b), subtracted = s::sub_saturate(a, b);
        for (std::size_t i = 0; i < V::size; ++i) {
            auto x = std::uint64_t(static_cast<U>(aa[i])), y = std::uint64_t(static_cast<U>(bb[i]));
            check(sum.lane(i) == fromBits<T>(x + y));
            check(difference.lane(i) == fromBits<T>(x - y));
            check(product.lane(i) == fromBits<T>(x * y));
            check(anded.lane(i) == fromBits<T>(x & y));
            check(ored.lane(i) == fromBits<T>(x | y));
            check(xored.lane(i) == fromBits<T>(x ^ y));
            check(eq.lane(i) == (aa[i] == bb[i]));
            check(lt.lane(i) == (aa[i] < bb[i]));
            check(le.lane(i) == (aa[i] <= bb[i]));
            check(ge.lane(i) == (aa[i] >= bb[i]));
            check(gt.lane(i) == (aa[i] > bb[i]));
            if constexpr (sizeof(T) < 8) {
                // Long double represents these sums exactly. Test 64-bit boundaries
                // separately.
                auto lo = static_cast<long double>(std::numeric_limits<T>::lowest());
                auto hi = static_cast<long double>(std::numeric_limits<T>::max());
                auto exact = static_cast<long double>(aa[i]) + static_cast<long double>(bb[i]);
                T expected = exact <= lo   ? std::numeric_limits<T>::lowest()
                             : exact >= hi ? std::numeric_limits<T>::max()
                                           : static_cast<T>(exact);
                check(saturated.lane(i) == expected);
                exact = static_cast<long double>(aa[i]) - static_cast<long double>(bb[i]);
                expected = exact <= lo   ? std::numeric_limits<T>::lowest()
                           : exact >= hi ? std::numeric_limits<T>::max()
                                         : static_cast<T>(exact);
                check(subtracted.lane(i) == expected);
            } else {
                T expectedSum = fromBits<T>(x + y), expectedDifference = fromBits<T>(x - y);
                if constexpr (std::is_unsigned_v<T>) {
                    if (expectedSum < aa[i])
                        expectedSum = std::numeric_limits<T>::max();
                    if (aa[i] < bb[i])
                        expectedDifference = 0;
                } else {
                    if ((aa[i] < 0) == (bb[i] < 0) && (expectedSum < 0) != (aa[i] < 0))
                        expectedSum = aa[i] < 0 ? std::numeric_limits<T>::lowest()
                                                : std::numeric_limits<T>::max();
                    if ((aa[i] < 0) != (bb[i] < 0) && (expectedDifference < 0) != (aa[i] < 0))
                        expectedDifference = aa[i] < 0 ? std::numeric_limits<T>::lowest()
                                                       : std::numeric_limits<T>::max();
                }
                check(saturated.lane(i) == expectedSum && subtracted.lane(i) == expectedDifference);
            }
        }
        for (unsigned shift : {0u, 1u, unsigned(sizeof(T) * 8 - 1), unsigned(sizeof(T) * 8),
                               unsigned(sizeof(T) * 8 + 1)}) {
            auto left = s::shift_left(a, shift), right = s::shift_right_logical(a, shift);
            for (std::size_t i = 0; i < V::size; ++i) {
                auto x = std::uint64_t(static_cast<U>(aa[i]));
                check(left.lane(i) == fromBits<T>(shift >= sizeof(T) * 8 ? 0 : x << shift));
                check(right.lane(i) == fromBits<T>(shift >= sizeof(T) * 8 ? 0 : x >> shift));
            }
        }
        if constexpr (std::is_unsigned_v<T>) {
            auto average = s::average_round_up(a, b);
            for (std::size_t i = 0; i < V::size; ++i) {
                auto expected =
                    static_cast<T>(aa[i] / 2 + bb[i] / 2 + ((aa[i] % 2 + bb[i] % 2 + 1) / 2));
                check(average.lane(i) == expected);
            }
        } else {
            for (unsigned n : {0u, 1u, unsigned(sizeof(T) * 8), unsigned(sizeof(T) * 8 + 1)}) {
                auto shifted = s::shift_right_arithmetic(V{T{-1}}, n);
                check(s::all(shifted == V{T{-1}}));
                auto actual = s::shift_right_arithmetic(a, n);
                for (std::size_t i = 0; i < V::size; ++i)
                    check(actual.lane(i) ==
                          static_cast<T>(std::floor(
                              std::ldexp(static_cast<long double>(aa[i]), -static_cast<int>(n)))));
            }
        }
    }
    auto hi = V{std::numeric_limits<T>::max()}, lo = V{std::numeric_limits<T>::lowest()};
    check(s::all(s::add_saturate(hi, V{1}) == hi));
    check(s::all(s::sub_saturate(lo, V{1}) == lo));
    check(s::all(s::add_saturate(hi, V{}) == hi));
    check(s::all(s::sub_saturate(hi, hi) == V{}));
    if constexpr (std::is_signed_v<T>) {
        check(s::all(s::add_saturate(lo, V{T{-1}}) == lo));
        check(s::all(s::sub_saturate(hi, V{T{-1}}) == hi));
        check(s::all(s::sub_saturate(lo, lo) == V{}));
    }
    V patterned;
    for (std::size_t i = 0; i < V::size; ++i)
        patterned.set_lane(i, static_cast<T>(i + 1));
    check(s::reduce_min(patterned) == T{1});
    check(s::reduce_max(patterned) == static_cast<T>(V::size));
    check(s::reduce_sum(patterned) == fromBits<T>(V::size * (V::size + 1) / 2));
}
template <class T, class A> void masksAndMemory() {
    using V = s::Batch<T, A>;
    using M = typename V::mask_type;
    static_assert(!std::is_convertible_v<M, bool>);
    std::array<T, V::size + 2> buffer{};
    V sequence;
    for (std::size_t i = 0; i < V::size; ++i)
        sequence.set_lane(i, static_cast<T>(i + 1));
    auto const patterns = std::size_t{1} << std::min(V::size, std::size_t{16});
    for (std::size_t bits = 0; bits < patterns; ++bits) {
        M mask;
        for (std::size_t i = 0; i < V::size; ++i)
            mask.set_lane(i, ((bits >> (i % 16)) & 1) != 0);
        std::size_t expected = 0;
        std::optional<std::size_t> first;
        for (std::size_t i = 0; i < V::size; ++i)
            if (mask.lane(i)) {
                ++expected;
                if (!first)
                    first = i;
            }
        check(s::count(mask) == expected);
        check(s::first_true(mask) == first);
        check(s::any(mask) == (expected != 0));
        check(s::all(mask) == (expected == V::size));
        check(s::none(mask & ~mask));
        check(s::all(mask | ~mask));
        buffer.fill(T{99});
        auto written = s::compress_store(buffer.data() + 1, sequence, mask);
        auto compressed = s::compress(sequence, mask);
        check(written == expected && compressed.count == expected);
        std::size_t j = 0;
        for (std::size_t i = 0; i < V::size; ++i)
            if (mask.lane(i)) {
                check(buffer[1 + j] == sequence.lane(i));
                check(compressed.values.lane(j++) == sequence.lane(i));
            }
        check(buffer.front() == T{99});
        for (std::size_t i = compressed.count; i < V::size; ++i)
            check(compressed.values.lane(i) == T{});
        for (std::size_t i = written + 1; i < buffer.size(); ++i)
            check(buffer[i] == T{99});
    }
    for (std::size_t n = 0; n <= V::size; ++n) {
        auto prefix = s::first_n<V>(n);
        check(s::count(prefix) == n);
        buffer.fill(T{99});
        s::store_partial(buffer.data() + 1, sequence, n);
        auto loaded = s::load_partial<V>(buffer.data() + 1, n, T{77});
        for (std::size_t i = 0; i < V::size; ++i)
            check(loaded.lane(i) == (i < n ? static_cast<T>(i + 1) : T{77}));
        check(buffer[0] == T{99} && buffer[n + 1] == T{99});
    }
    check(s::all(s::load_partial<V>(nullptr, 0, T{7}) == V{T{7}}));
    s::store_partial(static_cast<T *>(nullptr), sequence, 0);
    check(s::compress_store(static_cast<T *>(nullptr), sequence, M{}) == 0);
    alignas(V::alignment) std::array<T, V::size> aligned{};
    s::store_aligned(aligned.data(), sequence);
    check(s::all(s::load_aligned<V>(aligned.data()) == sequence));
    for (std::size_t offset = 0; offset < V::size; ++offset) {
        std::array<T, 2 * V::size> memory{};
        s::store(memory.data() + offset, sequence);
        check(s::all(s::load<V>(memory.data() + offset) == sequence));
    }
#if defined(__unix__) || defined(__ANDROID__)
    auto pageSize = sysconf(_SC_PAGESIZE);
    check(pageSize > 0);
    auto size = static_cast<std::size_t>(pageSize);
    void *mapping =
        mmap(nullptr, 2 * size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED);
    struct Mapping {
        void *base;
        std::size_t bytes;
        ~Mapping() { munmap(base, bytes); }
    } owner{mapping, 2 * size};
    auto *guard = static_cast<unsigned char *>(mapping) + size;
    check(mprotect(guard, size, PROT_NONE) == 0);
    for (std::size_t n = 1; n <= V::size; ++n) {
        auto *p = reinterpret_cast<T *>(guard - n * sizeof(T));
        s::store_partial(p, sequence, n);
        auto v = s::load_partial<V>(p, n);
        check(s::count(v == sequence) == n);
        check(s::compress_store(p, sequence, s::first_n<V>(n)) == n);
        typename V::mask_type suffix;
        for (std::size_t i = 0; i < V::size; ++i)
            suffix.set_lane(i, i >= V::size - n);
        check(s::compress_store(p, sequence, suffix) == n);
        for (std::size_t i = 0; i < n; ++i)
            check(p[i] == sequence.lane(V::size - n + i));
    }
    // The last incomplete input block must never trigger a complete vector store.
    for (auto write : {s::FilterWrite::Exact, s::FilterWrite::AllowOverwrite})
        for (std::size_t n = 1; n <= 3 * V::size + 1; ++n)
            for (unsigned pattern = 0; pattern < 4; ++pattern) {
                auto *p = reinterpret_cast<T *>(guard - n * sizeof(T));
                std::vector<T> input(n), expected;
                for (std::size_t i = 0; i < n; ++i) {
                    input[i] = static_cast<T>(pattern == 0   ? 0
                                              : pattern == 1 ? 1
                                              : pattern == 2 ? i % 2
                                                             : i + 1 < n);
                    if (input[i] != T{})
                        expected.push_back(input[i]);
                }
                auto predicate = [](V v) { return v != V{}; };
                check((s::copy_if<T, A>(input.data(), n, p, predicate, write) == expected.size()));
                check(std::equal(expected.begin(), expected.end(), p));
                std::copy(input.begin(), input.end(), p);
                check((s::copy_if<T, A>(p, n, p, predicate, write) == expected.size()));
                check(std::equal(expected.begin(), expected.end(), p));
            }
#endif
}
template <class T, class A> void rearrange() {
    using V = s::Batch<T, A>;
    V a, b;
    for (std::size_t i = 0; i < V::size; ++i) {
        a.set_lane(i, static_cast<T>(i));
        b.set_lane(i, static_cast<T>(i + V::size));
    }
    auto d = s::deinterleave(a, b);
    for (std::size_t i = 0; i < V::size; ++i) {
        check(d.even.lane(i) == static_cast<T>(2 * i));
        check(d.odd.lane(i) == static_cast<T>(2 * i + 1));
    }
    auto z = s::interleave(a, b);
    for (std::size_t i = 0; i < 2 * V::size; ++i) {
        T expected = static_cast<T>(i % 2 ? V::size + i / 2 : i / 2);
        check((i < V::size ? z.low.lane(i) : z.high.lane(i - V::size)) == expected);
    }
    auto rotated = s::rotate_left<1>(a);
    for (std::size_t i = 0; i < V::size; ++i)
        check(rotated.lane(i) == static_cast<T>((i + 1) % V::size));
}
template <class T, class A> void floating() {
    using V = s::Batch<T, A>;
    T const values[]{T{},
                     -T{},
                     T{1},
                     T{-1},
                     std::numeric_limits<T>::infinity(),
                     -std::numeric_limits<T>::infinity(),
                     std::numeric_limits<T>::quiet_NaN(),
                     std::numeric_limits<T>::denorm_min()};
    for (auto a : values)
        for (auto b : values) {
            V x{a}, y{b};
            check(s::all(x == y) == (a == b));
            check(s::all(x != y) == (a != b));
            check(s::all(x < y) == (a < b));
            check(s::all(x > y) == (a > b));
            check(s::all(x <= y) == (a <= b));
            check(s::all(x >= y) == (a >= b));
            auto plus = (x + y).lane(0), product = (x * y).lane(0);
            check((plus == a + b) || (std::isnan(plus) && std::isnan(a + b)));
            check((product == a * b) || (std::isnan(product) && std::isnan(a * b)));
        }
    check(std::signbit((-V{T{0}}).lane(0)));
    check(!std::signbit((-V{-T{0}}).lane(0)));
    auto epsilon = std::ldexp(T{1}, -int(std::numeric_limits<T>::digits / 2 + 1));
    auto result = s::fma(V{T{1} + epsilon}, V{T{1} - epsilon}, V{T{-1}});
    check(result.lane(0) == std::fma(T{1} + epsilon, T{1} - epsilon, T{-1}));
    check(result.lane(0) != T{});
    check(((V{T{1} + epsilon} * V{T{1} - epsilon}) + V{T{-1}}).lane(0) == T{});
}
template <class A> void conversions() {
    using U8 = s::Batch<std::uint8_t, A>;
    U8 bytes;
    for (std::size_t i = 0; i < U8::size; ++i)
        bytes.set_lane(i, static_cast<std::uint8_t>(i * 7));
    auto wide = s::widen<std::uint16_t>(bytes);
    using W = typename decltype(wide)::batch_type;
    for (std::size_t i = 0; i < U8::size; ++i)
        check(wide.parts[i / W::size].lane(i % W::size) == bytes.lane(i));
    check(s::all(s::narrow_saturate<std::uint8_t>(wide) == bytes));
    for (auto &part : wide.parts)
        part = typename decltype(wide)::batch_type{511};
    check(s::all(s::narrow_saturate<std::uint8_t>(wide) == U8{255}));
    check(s::all(s::narrow_truncate<std::uint8_t>(wide) == U8{255}));
    using F = s::Batch<float, A>;
    using I = s::Batch<std::int32_t, A>;
    float const inputs[]{-1.5f, -0.5f, 0.5f, 1.5f, 2.5f, 3.5f};
    int const expected[]{-2, 0, 0, 2, 2, 4};
    for (std::size_t i = 0; i < 6; ++i)
        check(s::all(s::convert_saturate<std::int32_t>(F{inputs[i]}, s::Rounding::NearestEven) ==
                     I{expected[i]}));
    check(s::all(s::convert_saturate<std::int32_t>(F{std::numeric_limits<float>::quiet_NaN()},
                                                   s::Rounding::TowardZero) == I{}));
    check(s::all(s::convert_saturate<std::int32_t>(F{std::numeric_limits<float>::infinity()},
                                                   s::Rounding::TowardZero) == I{INT32_MAX}));
    check(s::all(s::convert_saturate<std::int32_t>(F{-std::numeric_limits<float>::infinity()},
                                                   s::Rounding::TowardZero) == I{INT32_MIN}));
    check(s::all(s::convert<float>(I{42}) == F{42}));
    check(s::all(s::bit_cast<float>(s::bit_cast<std::uint32_t>(F{-0.0f})) == F{}));
    check(std::signbit(s::bit_cast<float>(s::bit_cast<std::uint32_t>(F{-0.0f})).lane(0)));
}
template <class A> void algorithms() {
    using V = s::Batch<std::int32_t, A>;
    auto predicate = [](V v) { return v == V{}; };
    check((!s::mismatch<std::int32_t, A>(nullptr, nullptr, 0)));
    check((!s::find_if<std::int32_t, A>(nullptr, 0, predicate)));
    check((s::copy_if<std::int32_t, A>(nullptr, 0, nullptr, predicate) == 0));
    std::array<std::int32_t, 3> moveInput{0, 1, 0}, moveOutput{};
    auto moveOnly = [] {
        return [value = std::make_unique<std::int32_t>(0)](V v) { return v == V{*value}; };
    };
    check((s::copy_if<std::int32_t, A>(moveInput.data(), moveInput.size(), moveOutput.data(),
                                       moveOnly()) == 2));
    for (auto mode : {s::FilterWrite::Exact, s::FilterWrite::AllowOverwrite})
        check((s::copy_if<std::int32_t, A>(moveInput.data(), moveInput.size(), moveOutput.data(),
                                           moveOnly(), mode) == 2));
    for (std::size_t n = 0; n < 3 * V::size + 3; ++n) {
        std::vector<std::int32_t> input(n), copy, output(n + 1, 99);
        std::vector<std::int32_t> expected;
        for (std::size_t i = 0; i < n; ++i) {
            input[i] = static_cast<int>(i % 3);
            if (input[i] == 0)
                expected.push_back(input[i]);
        }
        copy = input;
        check((!s::mismatch<std::int32_t, A>(input.data(), copy.data(), n)));
        for (std::size_t i = 0; i < n; ++i) {
            copy[i] = -1;
            check((s::mismatch<std::int32_t, A>(input.data(), copy.data(), n) == i));
            copy[i] = input[i];
        }
        auto written = s::copy_if<std::int32_t, A>(input.data(), n, output.data(), predicate);
        check(written == expected.size() &&
              std::equal(expected.begin(), expected.end(), output.begin()));
        check(output[written] == 99);
        written = s::copy_if<std::int32_t, A>(copy.data(), n, copy.data(), predicate);
        check(written == expected.size() &&
              std::equal(expected.begin(), expected.end(), copy.begin()));
    }
}
template <class T, class A> void type() {
    masksAndMemory<T, A>();
    rearrange<T, A>();
    if constexpr (std::is_integral_v<T>)
        integers<T, A>();
    else
        floating<T, A>();
}
#include "MoreContracts.hpp"
template <class A> void run() {
    type<std::uint8_t, A>();
    type<std::int8_t, A>();
    type<std::uint16_t, A>();
    type<std::int16_t, A>();
    type<std::uint32_t, A>();
    type<std::int32_t, A>();
    type<std::uint64_t, A>();
    type<std::int64_t, A>();
    type<float, A>();
    type<double, A>();
    conversions<A>();
    algorithms<A>();
    moreContracts<A>();
}
} // namespace simd_test
