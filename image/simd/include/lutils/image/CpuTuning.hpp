#pragma once
#include <chrono>
#include <cstdint>
#include <lutils/image/SimdConversion.hpp>
#include <vector>

namespace lutils::image {
enum class CpuTuningOutcome : std::uint8_t {
    Selected,
    NoAlternative,
    NoGain, // No confirmed gain; not proof that every alternative is slower.
    WarmupUnstable,
    InsufficientBudget, // A phase ended before enough evidence, within the total budget.
    BudgetExhausted
};
struct CpuTuningOptions {
    // Checked between conversions; a conversion already in flight must finish.
    std::chrono::milliseconds budget{100};
};
struct CpuTargetTiming {
    simd::Target target;
    std::vector<double> nanoseconds; // Probe and completed pair means, per conversion.
    double medianNanoseconds;
};
struct CpuTuningResult {
    CpuConversionPlan plan;
    std::vector<CpuTargetTiming> timings;
    std::chrono::nanoseconds elapsed;
    bool budgetExhausted;
    CpuTuningOutcome outcome = CpuTuningOutcome::NoAlternative;
};
// Explicit, one-time calibration on caller-owned, exclusively accessible views.
// Writes destination repeatedly; input is unchanged. No threads or global
// cache. Reuse the returned plan for similar data layouts and execution
// conditions. A candidate must pass paired screening and independent
// confirmation. In each five-block stage median(candidate/default) + 2*MAD must
// be <= 0.95, with at least four wins. Unstable warmup or insufficient budget
// keeps the default. Timings are diagnostics with different sample counts, not
// a ranking table. Pixel-buffer allocation and mapping are excluded; view
// validation (including metadata allocations) is measured.
Result<CpuTuningResult> tuneCpuConversion(ConstFrameView const &source,
                                          FrameView const &destination,
                                          CpuTuningOptions options = {});
} // namespace lutils::image
