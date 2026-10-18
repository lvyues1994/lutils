#include "Extended.hpp"
#include "Benchmark.hpp"
#include <iomanip>
#include <limits>
using namespace benchmark;
namespace s = lutils::simd;
namespace {
struct Backend {
    char const *name;
    extended::Function *run;
};
std::vector<Backend> backends() {
    std::vector<Backend> result{{"scalar", extended::scalar}};
#if defined(LUTILS_SIMD_X86)
    if (s::supported(s::Target::Sse2))
        result.push_back({"sse2", extended::sse2});
    if (s::supported(s::Target::Avx2))
        result.push_back({"avx2", extended::avx2});
#elif defined(LUTILS_SIMD_ARM64)
    result.push_back({"neon", extended::neon});
#endif
    return result;
}
std::uint32_t next(std::uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
template <class T> T reference(float value, unsigned mode) {
    if (std::isnan(value))
        return 0;
    double x = value;
    if (mode == 0)
        x = std::trunc(x);
    if (mode == 2)
        x = std::floor(x);
    if (mode == 3)
        x = std::ceil(x);
    if (mode == 1) {
        auto low = std::floor(x), fraction = x - low;
        x = low + (fraction > 0.5 || (fraction == 0.5 && std::fmod(low, 2) != 0));
    }
    if (x <= static_cast<double>(std::numeric_limits<T>::lowest()))
        return std::numeric_limits<T>::lowest();
    if (x >= static_cast<double>(std::numeric_limits<T>::max()))
        return std::numeric_limits<T>::max();
    return static_cast<T>(x);
}
std::string fields(char const *op, Backend backend, std::size_t n, int density) {
    return "\"operation\":\"" + std::string(op) + "\",\"target\":\"" + backend.name +
           "\",\"scope\":\"kernel\",\"elements\":" + std::to_string(n) +
           ",\"element_offset\":1,\"positive_percent\":" + std::to_string(density);
}
template <class T> void conversions(std::size_t n, bool smoke, Clock::time_point deadline) {
    std::vector<float> in(n + 1);
    std::vector<T> out(n + 2), expected(n);
    std::uint32_t state = 93721;
    for (std::size_t i = 1; i <= n; ++i) {
        in[i] = static_cast<float>(static_cast<int>(next(state) % 1000000) - 500000) / 4;
        if (i % 31 == 0)
            in[i] = std::numeric_limits<float>::quiet_NaN();
        if (i % 29 == 0)
            in[i] = std::numeric_limits<float>::infinity();
        if (i % 23 == 0)
            in[i] = -std::numeric_limits<float>::infinity();
        if (i % 19 == 0)
            in[i] = 2147483648.0f;
    }
    char const *modes[]{"zero", "even", "down", "up"};
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (std::size_t i = 0; i < n; ++i)
            expected[i] = reference<T>(in[i + 1], mode);
        for (auto backend : backends()) {
            auto op =
                std::is_signed_v<T> ? extended::Operation::Signed : extended::Operation::Unsigned;
            extended::Data data{in.data() + 1, out.data() + 1, n, mode};
            auto run = [&] { return backend.run(op, data); };
            auto verify = [&](std::uint64_t size) {
                check(size == n && out.front() == T{77} && out.back() == T{77});
                check(std::equal(expected.begin(), expected.end(), out.begin() + 1));
            };
            auto initialize = [&] {
                std::fill(out.begin(), out.end(), T{77});
                for (std::size_t i = 0; i < n; ++i)
                    out[i + 1] = expected[i] ^ T { 1 };
                verify(run());
            };
            auto name =
                std::string(std::is_signed_v<T> ? "convert_sat_f32_i32_" : "convert_sat_f32_u32_") +
                modes[mode];
            measure(fields(name.c_str(), backend, n, 50), run, initialize, verify, smoke, deadline);
        }
    }
}
template <class T> void filters(std::size_t n, bool smoke, Clock::time_point deadline) {
    for (bool overwrite : {false, true})
        for (int density : {0, 50, 100}) {
            std::vector<T> in(n + 1), out(n + 2), expected;
            std::uint32_t state = 93721;
            for (std::size_t i = 1; i <= n; ++i) {
                in[i] = static_cast<T>(next(state) % 100 < static_cast<unsigned>(density)
                                           ? 101 + state % 154
                                           : state % 101);
                if (in[i] > 100)
                    expected.push_back(in[i]);
            }
            for (auto backend : backends()) {
                constexpr auto op = sizeof(T) == 1   ? extended::Operation::Filter8
                                    : sizeof(T) == 2 ? extended::Operation::Filter16
                                    : sizeof(T) == 4 ? extended::Operation::Filter32
                                                     : extended::Operation::Filter64;
                extended::Data data{in.data() + 1, out.data() + 1, n, 0, overwrite};
                auto run = [&] { return backend.run(op, data); };
                auto verify = [&](std::uint64_t size) {
                    check(size == expected.size() && out.front() == T{77} && out.back() == T{77});
                    check(std::equal(expected.begin(), expected.end(), out.begin() + 1));
                    if (!overwrite)
                        check(std::all_of(out.data() + 1 + size, out.data() + out.size(),
                                          [](T x) { return x == T{77}; }));
                };
                auto initialize = [&] {
                    std::fill(out.begin(), out.end(), T{77});
                    verify(run());
                };
                auto name = std::string("copy_if_u") + std::to_string(8 * sizeof(T));
                if (overwrite)
                    name += "_overwrite";
                measure(fields(name.c_str(), backend, n, density), run, initialize, verify, smoke,
                        deadline);
            }
        }
}
} // namespace
int main(int argc, char **argv) {
    try {
        bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
        bool filtersOnly = argc == 2 && std::string(argv[1]) == "--filters";
        if (argc > 1 && !smoke && !filtersOnly)
            throw std::runtime_error("usage: simd_extended_benchmark [--smoke|--filters]");
        std::cout << std::setprecision(12)
                  << "{\"type\":\"environment\",\"schema\":1,\"compiler\":\"" << __VERSION__
                  << "\",\"build\":\"" << LUTILS_BENCH_BUILD
                  << "\",\"seed\":93721,\"threads\":1,\"clock\":\"steady_clock\","
                     "\"smoke\":"
                  << (smoke ? "true" : "false")
                  << ",\"filters_only\":" << (filtersOnly ? "true" : "false") << "}\n";
        auto deadline = Clock::now() + std::chrono::seconds(45);
        for (auto n :
             smoke ? std::vector<std::size_t>{67} : std::vector<std::size_t>{4096, 4099, 262144}) {
            if (!filtersOnly) {
                conversions<std::int32_t>(n, smoke, deadline);
                conversions<std::uint32_t>(n, smoke, deadline);
            }
            filters<std::uint8_t>(n, smoke, deadline);
            filters<std::uint16_t>(n, smoke, deadline);
            filters<std::uint32_t>(n, smoke, deadline);
            filters<std::uint64_t>(n, smoke, deadline);
        }
        std::cout << "{\"type\":\"complete\"}\n";
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
