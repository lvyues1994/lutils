#include "Tuning.hpp"
#include <algorithm>

namespace lutils::image {
Result<CpuTuningResult> tuneCpuConversion(ConstFrameView const &source,
                                          FrameView const &destination, CpuTuningOptions options) {
    using Clock = std::chrono::steady_clock;
    if (options.budget.count() <= 0 || options.budget > std::chrono::seconds(10))
        return Error{ErrorCode::InvalidArgument, "CPU tuning budget must be in (0, 10s]"};
    auto start = Clock::now();
    auto deadline = start + options.budget;
    auto prepared = CpuConversionPlan::prepare(source.desc, destination.desc);
    if (!prepared)
        return prepared.error();
    auto fallback = prepared.value().target();
    // Validate bounds/aliasing before the first write; also warm the default path.
    auto valid = prepared.value().run(source, destination);
    if (!valid)
        return valid.error();
    std::vector<CpuConversionPlan> plans{prepared.value()};
    for (auto target :
         {simd::Target::Scalar, simd::Target::Sse2, simd::Target::Avx2, simd::Target::Neon}) {
        auto width = target == simd::Target::Avx2 ? 32u : target == simd::Target::Scalar ? 1u : 16u;
        if (target == fallback || source.desc.width < width)
            continue;
        auto p = CpuConversionPlan::prepare(source.desc, destination.desc, target);
        if (p)
            plans.push_back(std::move(p).value());
    }
    std::vector<CpuTargetTiming> timings;
    std::vector<std::size_t> iterations;
    bool exhausted = false;
    for (auto const &plan : plans) {
        timings.push_back({plan.target(), {}, 0});
        timings.back().nanoseconds.reserve(5);
        if (Clock::now() >= deadline) {
            exhausted = true;
            break;
        }
        auto before = Clock::now();
        auto result = plan.run(source, destination);
        if (!result)
            return result.error();
        auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before).count();
        iterations.push_back(static_cast<std::size_t>(std::clamp(
            std::int64_t{200000} / std::max(std::int64_t{1}, static_cast<std::int64_t>(ns)),
            std::int64_t{1}, std::int64_t{64})));
    }
    for (std::size_t round = 0; round < 5 && !exhausted; ++round) {
        std::vector<double> samples(plans.size());
        for (std::size_t i = 0; i < plans.size(); ++i) {
            auto index = (i + round) % plans.size(); // Rotate thermal/order bias.
            auto before = Clock::now();
            for (std::size_t n = 0; n < iterations[index]; ++n) {
                if (Clock::now() >= deadline) {
                    exhausted = true;
                    break;
                }
                auto result = plans[index].run(source, destination);
                if (!result)
                    return result.error();
            }
            if (exhausted)
                break;
            samples[index] =
                std::chrono::duration<double, std::nano>(Clock::now() - before).count() /
                static_cast<double>(iterations[index]);
        }
        if (!exhausted)
            for (std::size_t i = 0; i < plans.size(); ++i)
                timings[i].nanoseconds.push_back(samples[i]);
    }
    for (auto &t : timings) {
        auto sorted = t.nanoseconds;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.empty())
            t.medianNanoseconds = (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) / 2;
    }
    auto selected = simd_detail::chooseTiming(timings, fallback);
    for (auto &plan : plans)
        if (plan.target() == selected)
            return CpuTuningResult{
                std::move(plan), std::move(timings),
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start),
                exhausted || Clock::now() >= deadline};
    return Error{ErrorCode::InvalidArgument, "CPU tuning selected an absent implementation"};
}
} // namespace lutils::image
