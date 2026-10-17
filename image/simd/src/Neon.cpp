#include "Kernel.hpp"
namespace lutils::image::simd_detail {
void neonUyvy(ConstFrameView const &a, FrameView const &b) { uyvyFrame<simd::abi::neon>(a, b); }
void neonRgba(ConstFrameView const &a, FrameView const &b) { rgbaFrame<simd::abi::neon>(a, b); }
} // namespace lutils::image::simd_detail
