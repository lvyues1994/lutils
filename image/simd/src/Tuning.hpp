#pragma once
#include <cmath>
#include <lutils/image/CpuTuning.hpp>

namespace lutils::image::simd_detail {
inline simd::Target chooseTiming(std::vector<CpuTargetTiming> const &timings,
                                 simd::Target fallback) {
    double baseline = 0;
    for (auto const &t : timings)
        if (t.target == fallback && t.nanoseconds.size() >= 3)
            baseline = t.medianNanoseconds;
    if (!(baseline > 0) || !std::isfinite(baseline))
        return fallback;
    auto selected = fallback;
    double best = baseline;
    for (auto const &t : timings)
        if (t.nanoseconds.size() >= 3 && t.medianNanoseconds > 0 &&
            std::isfinite(t.medianNanoseconds) && t.medianNanoseconds < best &&
            t.medianNanoseconds <= baseline * 0.95) {
            selected = t.target;
            best = t.medianNanoseconds;
        }
    return selected;
}
} // namespace lutils::image::simd_detail
