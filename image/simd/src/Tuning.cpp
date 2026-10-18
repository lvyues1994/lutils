#include "Tuning.hpp"

namespace lutils::image {
Result<CpuTuningResult> tuneCpuConversion(ConstFrameView const &source,
                                          FrameView const &destination, CpuTuningOptions options) {
    using Clock = std::chrono::steady_clock;
    if (options.budget.count() <= 0 || options.budget > std::chrono::seconds(10))
        return Error{ErrorCode::InvalidArgument, "CPU tuning budget must be in (0, 10s]"};
    auto start = Clock::now();
    auto prepared = CpuConversionPlan::prepare(source.desc, destination.desc);
    if (!prepared)
        return prepared.error();
    // Validate all bounds and aliasing before the first write.
    auto valid = prepared.value().run(source, destination);
    if (!valid)
        return valid.error();
    std::vector<CpuConversionPlan> plans{prepared.value()};
    for (auto target :
         {simd::Target::Avx2, simd::Target::Neon, simd::Target::Sse2, simd::Target::Scalar}) {
        auto width = target == simd::Target::Avx2 ? 32u : target == simd::Target::Scalar ? 1u : 16u;
        if (target == prepared.value().target() || source.desc.width < width)
            continue;
        auto plan = CpuConversionPlan::prepare(source.desc, destination.desc, target);
        if (plan)
            plans.push_back(std::move(plan).value());
    }
    auto measured = simd_detail::calibrate(
        plans.size(), options.budget, start, [] { return Clock::now(); },
        [&](std::size_t index) { return plans[index].run(source, destination); });
    if (!measured)
        return measured.error();
    auto result = std::move(measured).value();
    std::vector<CpuTargetTiming> timings;
    for (std::size_t i = 0; i < plans.size(); ++i) {
        auto median = result.samples[i].empty() ? 0 : simd_detail::median(result.samples[i]);
        timings.push_back({plans[i].target(), std::move(result.samples[i]), median});
    }
    auto end = Clock::now();
    return CpuTuningResult{std::move(plans[result.selected]), std::move(timings),
                           std::chrono::duration_cast<std::chrono::nanoseconds>(end - start),
                           end >= start + options.budget, result.outcome};
}
} // namespace lutils::image
