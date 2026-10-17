#include "Micro.hpp"
#include "Benchmark.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <lutils/image/SimdConversion.hpp>
#if __has_include(<lutils/image/CpuTuning.hpp>)
#include <lutils/image/CpuTuning.hpp>
#define LUTILS_BENCH_TUNING 1
#endif
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif
namespace s = lutils::simd;
namespace im = lutils::image;
using namespace benchmark;
namespace {
struct Backend {
    s::Target target;
    char const *name;
    micro::Function *function;
};
std::vector<Backend> backends() {
    std::vector<Backend> out{{s::Target::Scalar, "scalar", micro::scalar}};
#if defined(LUTILS_SIMD_X86)
    if (s::supported(s::Target::Sse2))
        out.push_back({s::Target::Sse2, "sse2", micro::sse2});
    if (s::supported(s::Target::Avx2))
        out.push_back({s::Target::Avx2, "avx2", micro::avx2});
#elif defined(LUTILS_SIMD_ARM64)
    out.push_back({s::Target::Neon, "neon", micro::neon});
#endif
    return out;
}
void primitives(bool smoke, Clock::time_point deadline) {
    auto sizes =
        smoke ? std::vector<std::size_t>{67} : std::vector<std::size_t>{4096, 4099, 262144};
    auto targets = backends();
    for (auto n : sizes) {
        // One element offset exercises unaligned vector accesses without violating T alignment.
        std::vector<std::uint8_t> bytes(n + 1), narrow(n + 1);
        std::vector<std::uint16_t> words(n + 1), wide(n + 1);
        std::vector<std::int32_t> integers(n + 1), filtered(n + 1), reference;
        std::vector<float> floats(n + 1);
        micro::Data data{bytes.data() + 1,  words.data() + 1,  integers.data() + 1, wide.data() + 1,
                         narrow.data() + 1, floats.data() + 1, filtered.data() + 1, n};
        for (std::size_t i = 1; i <= n; ++i) {
            bytes[i] = static_cast<std::uint8_t>(i * 97);
            words[i] = static_cast<std::uint16_t>(i * 937);
        }
        int ordinal = 0;
        for (auto op :
             {micro::Operation::Widen, micro::Operation::Narrow, micro::Operation::Convert,
              micro::Operation::Filter, micro::Operation::Sum}) {
            auto densities = op == micro::Operation::Filter ? std::vector<int>{0, 50, 100}
                                                            : std::vector<int>{50};
            for (auto density : densities) {
                std::uint32_t state = 93721, sum = 0;
                reference.clear();
                for (std::size_t i = 1; i <= n; ++i) {
                    state ^= state << 13;
                    state ^= state >> 17;
                    state ^= state << 5;
                    integers[i] = (state % 100 < static_cast<unsigned>(density) ? 1 : -1) *
                                  static_cast<int>(1 + (state % 10000));
                    sum += static_cast<std::uint32_t>(integers[i]);
                    if (integers[i] > 0)
                        reference.push_back(integers[i]);
                }
                char const *names[]{"widen_u8_u16", "narrow_sat_u16_u8", "convert_i32_f32",
                                    "copy_if_i32", "sum_u32"};
                for (std::size_t t = 0; t < targets.size(); ++t) {
                    auto const &backend =
                        targets[(t + static_cast<std::size_t>(ordinal)) % targets.size()];
                    auto run = [&]() { return backend.function(op, data); };
                    auto verify = [&](std::uint64_t result) {
                        if (op == micro::Operation::Sum)
                            check(result == sum);
                        else if (op == micro::Operation::Filter) {
                            check(result == reference.size());
                            check(std::equal(reference.begin(), reference.end(), data.filtered));
                            check(std::all_of(data.filtered + result, data.filtered + n,
                                              [](std::int32_t x) { return x == INT32_MIN; }));
                        } else
                            for (std::size_t i = 0; i < n; ++i) {
                                if (op == micro::Operation::Widen)
                                    check(data.wide[i] == data.bytes[i]);
                                if (op == micro::Operation::Narrow)
                                    check(data.narrow[i] ==
                                          std::min(data.words[i], std::uint16_t{255}));
                                if (op == micro::Operation::Convert)
                                    check(data.floats[i] == static_cast<float>(data.integers[i]));
                            }
                    };
                    auto validate = [&]() {
                        for (std::size_t i = 0; i < n; ++i) {
                            data.wide[i] = static_cast<std::uint16_t>(data.bytes[i] + 1u);
                            data.narrow[i] = static_cast<std::uint8_t>(
                                std::min(data.words[i], std::uint16_t{255}) ^ 1);
                            data.floats[i] = std::numeric_limits<float>::quiet_NaN();
                            data.filtered[i] = INT32_MIN;
                        }
                        verify(run());
                    };
                    std::string fields =
                        "\"operation\":\"" + std::string(names[static_cast<int>(op)]) +
                        "\",\"target\":\"" + backend.name +
                        "\",\"scope\":\"kernel\",\"elements\":" + std::to_string(n) +
                        ",\"element_offset\":1,\"positive_percent\":" + std::to_string(density);
                    measure(fields, run, validate, verify, smoke, deadline);
                }
                ++ordinal;
            }
        }
    }
}
void images(bool smoke, Clock::time_point deadline) {
    struct Shape {
        std::uint32_t width, height;
        std::size_t alignment;
    };
    auto shapes = smoke ? std::vector<Shape>{{66, 5, 64}}
                        : std::vector<Shape>{{2, 5, 1}, {14, 5, 1}, {16, 5, 1}, {30, 5, 1},
                                             {32, 5, 1}, {34, 5, 1}, {640, 480, 1},
                                             {1920, 1536, 1}, {642, 481, 64}};
    for (auto shape : shapes)
        for (bool rgba : {false, true}) {
            im::FrameDesc desc{shape.width,
                               shape.height,
                               rgba ? im::nv12() : im::uyvy422(),
                               {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                                rgba ? im::ChromaLocation::Midpoint : im::ChromaLocation::Cosited}};
            auto outDesc = desc;
            outDesc.format = rgba ? im::rgba8() : im::nv12();
            if (rgba)
                outDesc.color = {im::Matrix::Identity, im::Range::Full};
            else
                outDesc.color.vertical = im::ChromaLocation::Midpoint;
            auto input = take(im::HostFrame::create(desc, shape.alignment));
            auto output = take(im::HostFrame::create(outDesc, shape.alignment));
            auto reference = take(im::HostFrame::create(outDesc, shape.alignment));
            auto inputView = input.view();
            auto src = im::readOnly(inputView);
            auto dst = output.view();
            auto srcGeometry = take(im::geometry(desc)), dstGeometry = take(im::geometry(outDesc));
            std::uint32_t state = 93721;
            for (std::size_t p = 0; p < srcGeometry.size(); ++p)
                for (std::size_t y = 0; y < srcGeometry[p].rows; ++y)
                    for (std::size_t x = 0; x < srcGeometry[p].rowBytes; ++x) {
                        state ^= state << 13;
                        state ^= state >> 17;
                        state ^= state << 5;
                        im::row(inputView.planes[p], y)[x] = std::byte(state & 255);
                    }
            auto ref = reference.view();
            imageReference(src, ref, rgba);
            auto validate = [&]() {
                for (std::size_t p = 0; p < dstGeometry.size(); ++p)
                    for (std::size_t y = 0; y < dstGeometry[p].rows; ++y)
                        check(std::equal(im::row(ref.planes[p], y),
                                         im::row(ref.planes[p], y) + dstGeometry[p].rowBytes,
                                         im::row(dst.planes[p], y)));
            };
            auto targets = backends();
            targets.push_back({s::Target::Automatic, "automatic", nullptr});
#ifdef LUTILS_BENCH_TUNING
            targets.push_back({s::Target::Automatic, "tuned", nullptr});
#endif
            for (auto const &backend : targets) {
                auto plan = take(im::CpuConversionPlan::prepare(desc, outDesc, backend.target));
                std::int64_t calibration = 0;
#ifdef LUTILS_BENCH_TUNING
                if (std::string(backend.name) == "tuned") {
                    auto tuned = take(im::tuneCpuConversion(src, dst));
                    plan = std::move(tuned.plan);
                    calibration = tuned.elapsed.count();
                }
#endif
                for (bool prepare : {false, true}) {
                    if (prepare && std::string(backend.name) == "tuned")
                        continue;
                    auto run = [&]() {
                        if (prepare) {
                            auto p =
                                take(im::CpuConversionPlan::prepare(desc, outDesc, backend.target));
                            take(p.run(src, dst));
                        } else
                            take(plan.run(src, dst));
                        return std::uint64_t(
                            std::to_integer<unsigned>(im::row(dst.planes[0], 0)[0]));
                    };
                    auto validateRun = [&]() {
                        for (std::size_t p = 0; p < dstGeometry.size(); ++p)
                            for (std::size_t y = 0; y < dstGeometry[p].rows; ++y)
                                for (std::size_t x = 0; x < dstGeometry[p].rowBytes; ++x)
                                    im::row(dst.planes[p], y)[x] =
                                        im::row(ref.planes[p], y)[x] ^ std::byte{1};
                        consumed = run();
                        validate();
                    };
                    std::string fields =
                        "\"operation\":\"" + std::string(rgba ? "nv12_rgba" : "uyvy_nv12") +
                        "\",\"target\":\"" + backend.name + "\",\"scope\":\"" +
                        (prepare ? "prepare_and_run" : "prepared_run") +
                        "\",\"width\":" + std::to_string(shape.width) +
                        ",\"height\":" + std::to_string(shape.height) +
                        ",\"row_alignment\":" + std::to_string(shape.alignment) + ",\"elements\":" +
                        std::to_string(std::uint64_t(shape.width) * shape.height) +
                        ",\"selected_target\":\"" + s::target_name(plan.target()) +
                        "\",\"calibration_ns\":" + std::to_string(calibration);
                    measure(fields, run, validateRun, [&](std::uint64_t) { validate(); }, smoke,
                            deadline);
                }
            }
        }
}
} // namespace
int main(int argc, char **argv) {
    try {
        bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
        if (argc > 1 && !smoke)
            throw std::runtime_error("usage: simd_benchmark [--smoke]");
        std::cout << std::setprecision(12)
                  << "{\"type\":\"environment\",\"schema\":1,\"compiler\":\"" << __VERSION__
                  << "\",\"build\":\"" << LUTILS_BENCH_BUILD
                  << "\",\"seed\":93721,\"threads\":1,\"clock\":\"steady_clock\",\"smoke\":"
                  << (smoke ? "true" : "false") << "}\n";
        auto deadline = Clock::now() + std::chrono::seconds(45);
        primitives(smoke, deadline);
        images(smoke, deadline);
        std::cout << "{\"type\":\"complete\"}\n";
        return 0;
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
