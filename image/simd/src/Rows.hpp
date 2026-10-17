#pragma once
#include <cstddef>
#include <cstdint>
#include <lutils/image/Frame.hpp>

namespace lutils::image::simd_detail {
using Kernel = void(ConstFrameView const &, FrameView const &);
Kernel scalarUyvy, scalarRgba;
Kernel sse2Uyvy, sse2Rgba;
Kernel avx2Uyvy, avx2Rgba;
Kernel neonUyvy, neonRgba;
} // namespace lutils::image::simd_detail
