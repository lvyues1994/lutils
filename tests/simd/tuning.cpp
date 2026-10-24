#include "../../image/simd/src/Tuning.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace im = lutils::image;
namespace s = lutils::simd;
namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("CPU tuning contract failed");
}
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void choose() {
    using im::simd_detail::PairSamples;
    using im::simd_detail::stableGain;
    auto samples = [](double value) {
        return PairSamples{{100, 100, 100, 100, 100}, {value, value, value, value, value}};
    };
    check(!stableGain({}));
    check(stableGain(samples(90)).has_value());
    check(stableGain(samples(95)).has_value());
    for (double x : {96.0, 100.0, -1.0, 0.0, double(NAN), double(INFINITY)})
        check(!stableGain(samples(x)));
    auto noisy = samples(90);
    noisy.candidate = {80, 85, 95, 105, 115};
    check(!stableGain(noisy));
    noisy.candidate = {1, 100, 100, 100, 100};
    check(!stableGain(noisy));
    noisy.candidate = {90, 90, 90, 110, 110};
    check(!stableGain(noisy));
    noisy = samples(90);
    noisy.baseline.pop_back();
    check(!stableGain(noisy));
    check(im::simd_detail::confirmed(samples(90), samples(92)));
    check(!im::simd_detail::confirmed(samples(90), samples(110)));
    // Linear drift cancels inside each complete ABBA/BAAB block.
    PairSamples drift;
    unsigned calls = 0;
    auto complete =
        im::simd_detail::collectPairs(drift, [&](bool) -> lutils::Result<std::optional<double>> {
            return std::optional<double>{100.0 + double(++calls)};
        });
    check(complete && complete.value() && calls == 8);
    check(drift.baseline == drift.candidate && !stableGain(drift));
    // One non-win still permits four later wins, so retain all five blocks.
    PairSamples lateWinner;
    calls = 0;
    auto collected = im::simd_detail::collectPairs(
        lateWinner, [&](bool candidate) -> lutils::Result<std::optional<double>> {
            auto round = calls++ / 4;
            return std::optional<double>{candidate && round > 0 ? 80.0 : 100.0};
        });
    check(collected && collected.value() && calls == 20 && stableGain(lateWinner));
    // Two initial ties cannot reach four wins even if every later block wins.
    PairSamples rejected;
    calls = 0;
    collected = im::simd_detail::collectPairs(
        rejected, [&](bool candidate) -> lutils::Result<std::optional<double>> {
            auto round = calls++ / 4;
            return std::optional<double>{candidate && round > 1 ? 80.0 : 100.0};
        });
    check(collected && collected.value() && calls == 8 && !stableGain(rejected));
    PairSamples partial;
    calls = 0;
    auto timeout =
        im::simd_detail::collectPairs(partial, [&](bool) -> lutils::Result<std::optional<double>> {
            if (++calls == 6)
                return std::optional<double>{};
            return std::optional<double>{100};
        });
    check(timeout && !timeout.value() && partial.baseline.size() == 1 &&
          partial.candidate.size() == 1);
    PairSamples failed;
    auto failure =
        im::simd_detail::collectPairs(failed, [](bool) -> lutils::Result<std::optional<double>> {
            return lutils::Error{lutils::ErrorCode::InvalidArgument, "injected execution failure"};
        });
    check(!failure && failed.baseline.empty());
}
void controller() {
    using namespace std::chrono;
    using Clock = steady_clock;
    unsigned completeCalls = 0;
    for (unsigned mode = 0; mode < 7; ++mode) {
        Clock::time_point now{};
        unsigned calls[2]{};
        auto result = im::simd_detail::calibrate(
            2, milliseconds{100}, now, [&] { return now; },
            [&](std::size_t index) -> lutils::Result<void> {
                ++calls[index];
                auto cost = microseconds{index == 0 ? 100 : 80};
                if (mode == 1 && index == 0)
                    cost = microseconds{calls[0] % 2 ? 100 : 500};
                if (mode == 2 && index == 1)
                    cost = microseconds{150};
                if (mode == 3 && index == 1 && calls[1] > 21)
                    cost = microseconds{120};
                if (mode == 4 && index == 1 && calls[1] > 1)
                    cost = milliseconds{30};
                if (mode == 6)
                    return lutils::Error{lutils::ErrorCode::InvalidArgument, "injected"};
                now += cost;
                if (mode == 5 && calls[0] + calls[1] == completeCalls)
                    now = Clock::time_point{} + milliseconds{101};
                return {};
            });
        if (mode == 6) {
            check(!result);
            continue;
        }
        auto value = take(std::move(result));
        check(value.selected == ((mode == 0 || mode == 5) ? 1u : 0u));
        im::CpuTuningOutcome const expected[]{im::CpuTuningOutcome::Selected,
                                              im::CpuTuningOutcome::WarmupUnstable,
                                              im::CpuTuningOutcome::NoGain,
                                              im::CpuTuningOutcome::NoGain,
                                              im::CpuTuningOutcome::InsufficientBudget,
                                              im::CpuTuningOutcome::Selected};
        check(value.outcome == expected[mode]);
        if (mode == 0) {
            completeCalls = calls[0] + calls[1];
            check(value.samples[1].size() == 11);
        }
        if (mode == 1)
            check(now.time_since_epoch() >= milliseconds{20} && calls[1] == 0);
        if (mode == 2)
            check(value.samples[1].size() == 1);
        if (mode == 4)
            check(value.samples[1].size() < 6);
    }
}
void smallBudgets() {
    using namespace std::chrono;
    for (auto ms : {1, 3, 4, 8, 16, 19, 20, 21, 23, 25, 100}) {
        steady_clock::time_point now{};
        auto value = take(im::simd_detail::calibrate(
            3, milliseconds{ms}, now, [&] { return now; },
            [&](std::size_t index) -> lutils::Result<void> {
                now += nanoseconds{index == 0 ? 1000 : index == 1 ? 800 : 500};
                return {};
            }));
        check(value.selected == 2 && value.outcome == im::CpuTuningOutcome::Selected);
        check(now.time_since_epoch() < milliseconds{ms});
    }
    for (auto cost : {microseconds{150}, microseconds{1100}}) {
        steady_clock::time_point now{};
        auto value = take(im::simd_detail::calibrate(
            2, milliseconds{1}, now, [&] { return now; },
            [&](std::size_t) -> lutils::Result<void> {
                now += cost;
                return {};
            }));
        check(value.selected == 0);
        check(value.outcome == (cost.count() == 150 ? im::CpuTuningOutcome::InsufficientBudget
                                                    : im::CpuTuningOutcome::BudgetExhausted));
    }
    steady_clock::time_point now{};
    auto single = take(im::simd_detail::calibrate(
        1, milliseconds{1}, now, [&] { return now; },
        [](std::size_t) -> lutils::Result<void> { throw std::runtime_error("unexpected run"); }));
    check(single.selected == 0 && single.outcome == im::CpuTuningOutcome::NoAlternative);
}
void images() {
    for (auto width : {2u, 14u, 16u, 30u, 32u, 34u})
        for (bool rgba : {false, true}) {
            im::FrameDesc desc{width,
                               5,
                               rgba ? im::nv12() : im::uyvy422(),
                               {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                                rgba ? im::ChromaLocation::Midpoint : im::ChromaLocation::Cosited}};
            auto dest = desc;
            dest.format = rgba ? im::rgba8() : im::nv12();
            if (rgba)
                dest.color = {im::Matrix::Identity, im::Range::Full};
            else
                dest.color.vertical = im::ChromaLocation::Midpoint;
            auto in = take(im::HostFrame::create(desc, 64));
            auto out = take(im::HostFrame::create(dest, 64));
            auto ref = take(im::HostFrame::create(dest, 64));
            auto view = in.view();
            auto geometry = take(im::geometry(desc));
            for (std::size_t p = 0; p < geometry.size(); ++p)
                for (std::size_t y = 0; y < geometry[p].rows; ++y)
                    for (std::size_t x = 0; x < geometry[p].rowBytes; ++x)
                        im::row(view.planes[p], y)[x] = std::byte((17 * x + 37 * y) & 255);
            auto input = im::readOnly(view);
            auto output = out.view();
            auto baseline = take(im::CpuConversionPlan::prepare(desc, dest));
            check(width >= 16 || baseline.target() == s::Target::Scalar);
            check(width >= 32 || baseline.target() != s::Target::Avx2);
            for (auto target : {s::Target::Sse2, s::Target::Avx2, s::Target::Neon}) {
                auto explicitPlan = im::CpuConversionPlan::prepare(desc, dest, target);
                if (explicitPlan)
                    check(explicitPlan.value().target() == target);
            }
            auto scalar = take(im::CpuConversionPlan::prepare(desc, dest, s::Target::Scalar));
            check(bool(scalar.run(input, ref.view())));
            auto tuned = take(im::tuneCpuConversion(input, output));
            check(s::supported(tuned.plan.target()) && tuned.elapsed.count() > 0);
            for (auto const &t : tuned.timings) {
                for (auto ns : t.nanoseconds)
                    check(ns > 0 && std::isfinite(ns));
                if (t.target == tuned.plan.target() && t.target != baseline.target())
                    check(t.nanoseconds.size() == 11);
            }
            auto outGeometry = take(im::geometry(dest));
            auto reference = ref.view();
            for (std::size_t p = 0; p < outGeometry.size(); ++p)
                for (std::size_t y = 0; y < outGeometry[p].rows; ++y)
                    check(std::equal(im::row(output.planes[p], y),
                                     im::row(output.planes[p], y) + outGeometry[p].rowBytes,
                                     im::row(reference.planes[p], y)));
            check(!im::tuneCpuConversion(input, output, {std::chrono::milliseconds{0}}));
            check(!im::tuneCpuConversion(input, output, {std::chrono::seconds{11}}));
            auto bad = output;
            bad.planes[0].capacity = 0;
            check(!im::tuneCpuConversion(input, bad));
            auto limited =
                take(im::tuneCpuConversion(input, output, {std::chrono::milliseconds{1}}));
            check(s::supported(limited.plan.target()));
        }
}
} // namespace
int main() {
    try {
        choose();
        controller();
        smallBudgets();
        images();
        std::cout << "CPU tuning contracts passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
