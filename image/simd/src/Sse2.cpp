#include "Kernel.hpp"
namespace lutils::image::simd_detail {
void sse2Uyvy(ConstFrameView const &a, FrameView const &b) { uyvyFrame<simd::abi::sse2>(a, b); }
void sse2Rgba(ConstFrameView const &a, FrameView const &b) { rgbaFrame<simd::abi::sse2>(a, b); }
} // namespace lutils::image::simd_detail
