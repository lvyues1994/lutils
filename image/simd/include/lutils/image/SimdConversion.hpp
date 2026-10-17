#pragma once
#include <lutils/image/Frame.hpp>
#include <lutils/simd/Dispatch.hpp>

namespace lutils::image {
// A validated, reusable CPU conversion. Resolves the ISA once during preparation.
// run() accepts arbitrary valid strides and performs no pixel staging copy.
class CpuConversionPlan {
  public:
    static Result<CpuConversionPlan> prepare(FrameDesc source, FrameDesc destination,
                                             simd::Target target = simd::Target::Automatic);
    Result<void> run(ConstFrameView const &source, FrameView const &destination) const;
    FrameDesc const &source() const noexcept { return source_; }
    FrameDesc const &destination() const noexcept { return destination_; }
    simd::Target target() const noexcept { return target_; }

  private:
    using Kernel = void (*)(ConstFrameView const &, FrameView const &);
    CpuConversionPlan(FrameDesc source, FrameDesc destination, Kernel kernel, simd::Target target);
    FrameDesc source_;
    FrameDesc destination_;
    Kernel kernel_;
    simd::Target target_;
};
// Progressive UYVY422 -> NV12, vertical box filter with upward rounding.
// The views are CPU-accessible; source and destination allocations must not overlap.
Result<void> convertUyvyToNv12(ConstFrameView const &source, FrameView const &destination,
                               simd::Target target = simd::Target::Automatic);
// BT.601 limited NV12 -> full-range RGBA8, nearest chroma, alpha 255.
// Uses the same fixed-point coefficients and rounding as the compute kernel.
Result<void> convertNv12ToRgba(ConstFrameView const &source, FrameView const &destination,
                               simd::Target target = simd::Target::Automatic);
} // namespace lutils::image
