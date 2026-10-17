#pragma once
#include <chrono>
#include <lutils/image/SimdConversion.hpp>
#include <vector>

namespace lutils::image {
struct CpuTuningOptions {
    // Checked between conversions; a conversion already in flight must finish.
    std::chrono::milliseconds budget{100};
};
struct CpuTargetTiming {
    simd::Target target;
    std::vector<double> nanoseconds; // Per conversion; equal complete rounds only.
    double medianNanoseconds;
};
struct CpuTuningResult {
    CpuConversionPlan plan;
    std::vector<CpuTargetTiming> timings;
    std::chrono::nanoseconds elapsed;
    bool budgetExhausted;
};
// Explicit, one-time calibration on caller-owned, exclusively accessible views.
// Writes destination repeatedly; input is unchanged. No threads or global cache.
// Reuse the returned plan for similar data layouts and execution conditions.
// Fewer than three complete rounds keeps Automatic's default; otherwise a
// candidate must beat it by at least 5%. Pixel-buffer allocation and mapping
// are excluded; view validation (including metadata allocations) is measured.
Result<CpuTuningResult> tuneCpuConversion(ConstFrameView const &source,
                                          FrameView const &destination,
                                          CpuTuningOptions options = {});
} // namespace lutils::image
