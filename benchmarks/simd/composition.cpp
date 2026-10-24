#include "Composition.hpp"
#include "Benchmark.hpp"
#include <cstring>
#include <iomanip>

namespace {
namespace s = lutils::simd;
namespace c = composition;
using namespace benchmark;
struct Backend {
    char const *name;
    c::Function *library;
    c::Function *intrinsic;
};
std::vector<Backend> backends(c::Operation op) {
    std::vector<Backend> result{{"scalar", c::scalar, nullptr}};
#if defined(LUTILS_SIMD_X86)
    if (s::supported(s::Target::Sse2))
        result.push_back({"sse2", c::sse2, op == c::Operation::Fma ? nullptr : c::nativeSse2});
    if (s::supported(s::Target::Avx2))
        result.push_back({"avx2", c::avx2, op == c::Operation::Fma ? nullptr : c::nativeAvx2});
    auto features = s::cpu_features();
    if (op == c::Operation::Fma && features.avx2 && features.fma)
        result.push_back({"avx2_fma", c::avx2Fma, c::nativeAvx2Fma});
#elif defined(LUTILS_SIMD_ARM64)
    (void)op;
    result.push_back({"neon", c::neon, c::nativeNeon});
#else
    (void)op;
#endif
    return result;
}
std::uint32_t next(std::uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
template <class T> T value(std::uint32_t &state) {
    auto bits = next(state);
    if constexpr (std::is_floating_point_v<T>)
        return static_cast<float>(static_cast<int>(bits % 100001) - 50000) / 127.0f;
    else {
        T result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
}
template <class T>
void test(c::Operation op, char const *name, std::size_t n, bool smoke,
          Clock::time_point deadline) {
    auto count = op == c::Operation::Zip || op == c::Operation::Unzip ? 2 * n
                 : op == c::Operation::Reduce                         ? (n + 7) / 8
                                                                      : n;
    std::vector<T> a(2 * n + 1), b(n + 1), yes(n + 1), no(n + 1), expected(count), out(count + 2);
    std::uint32_t state = 93721;
    for (auto *values : {&a, &b, &yes, &no})
        for (auto &x : *values)
            x = value<T>(state);
    for (std::size_t i = 0; i < n; ++i) {
        switch (op) {
        case c::Operation::Min:
            expected[i] = std::min(a[i + 1], b[i + 1]);
            break;
        case c::Operation::Max:
            expected[i] = std::max(a[i + 1], b[i + 1]);
            break;
        case c::Operation::SelectInt:
        case c::Operation::SelectFloat:
            expected[i] = a[i + 1] < b[i + 1] ? yes[i + 1] : no[i + 1];
            break;
        case c::Operation::Zip:
            expected[2 * i] = a[i + 1];
            expected[2 * i + 1] = b[i + 1];
            break;
        case c::Operation::Unzip:
            expected[i] = a[2 * i + 1];
            expected[n + i] = a[2 * i + 2];
            break;
        case c::Operation::Reduce:
            if constexpr (std::is_same_v<T, std::uint32_t>)
                expected[i / 8] += a[i + 1];
            break;
        case c::Operation::Fma:
            if constexpr (std::is_floating_point_v<T>)
                expected[i] = std::fma(a[i + 1], b[i + 1], yes[i + 1]);
            break;
        }
    }
    c::Data data{a.data() + 1, b.data() + 1, yes.data() + 1, no.data() + 1, out.data() + 1, n};
    for (auto backend : backends(op))
        for (bool intrinsic : {false, true}) {
            auto function = intrinsic ? backend.intrinsic : backend.library;
            if (!function)
                continue;
            auto run = [&] { return function(op, data); };
            auto verify = [&](std::uint64_t written) {
                check(written == count && out.front() == T{77} && out.back() == T{77});
                check(count == 0 ||
                      std::memcmp(expected.data(), out.data() + 1, count * sizeof(T)) == 0);
            };
            auto initialize = [&] {
                std::fill(out.begin(), out.end(), T{77});
                verify(run());
            };
            auto fields = "\"operation\":\"" + std::string(name) + "\",\"target\":\"" +
                          backend.name + "\",\"implementation\":\"" +
                          (intrinsic ? "intrinsic" : "library") +
                          "\",\"scope\":\"kernel\",\"elements\":" + std::to_string(n) +
                          ",\"element_offset\":1";
            measure(fields, run, initialize, verify, smoke, deadline);
        }
}
} // namespace
int main(int argc, char **argv) {
    try {
        bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
        if (argc > 1 && !smoke)
            throw std::runtime_error("usage: simd_composition_benchmark [--smoke]");
        std::cout << std::setprecision(12)
                  << "{\"type\":\"environment\",\"schema\":1,\"compiler\":\"" << __VERSION__
                  << "\",\"build\":\"" << LUTILS_BENCH_BUILD
                  << "\",\"seed\":93721,\"threads\":1,\"clock\":\"steady_clock\",\"smoke\":"
                  << (smoke ? "true" : "false") << "}\n";
        auto deadline = Clock::now() + std::chrono::seconds(45);
        auto sizes = smoke ? std::vector<std::size_t>{0, 1, 3, 4, 7, 8, 9, 15, 16, 17, 67}
                           : std::vector<std::size_t>{4096, 4099, 262144};
        for (auto n : sizes) {
            test<std::int32_t>(c::Operation::Min, "min_i32", n, smoke, deadline);
            test<std::uint32_t>(c::Operation::Max, "max_u32", n, smoke, deadline);
            test<std::int32_t>(c::Operation::SelectInt, "select_i32", n, smoke, deadline);
            test<float>(c::Operation::SelectFloat, "select_f32", n, smoke, deadline);
            test<float>(c::Operation::Zip, "interleave_f32", n, smoke, deadline);
            test<float>(c::Operation::Unzip, "deinterleave_f32", n, smoke, deadline);
            test<std::uint32_t>(c::Operation::Reduce, "reduce_u32_block8", n, smoke, deadline);
            test<float>(c::Operation::Fma, "fma_f32", n, smoke, deadline);
        }
        std::cout << "{\"type\":\"complete\"}\n";
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
