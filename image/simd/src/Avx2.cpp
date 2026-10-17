#include "Kernel.hpp"
namespace lutils::image::simd_detail {
void avx2Uyvy(ConstFrameView const &a, FrameView const &b) { uyvyFrame<simd::abi::avx2>(a, b); }
void avx2Rgba(ConstFrameView const &a, FrameView const &b) { rgbaFrame<simd::abi::avx2>(a, b); }
} // namespace lutils::image::simd_detail
