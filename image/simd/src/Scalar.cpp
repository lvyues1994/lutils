#include "Kernel.hpp"
namespace lutils::image::simd_detail {
void scalarUyvy(ConstFrameView const &a, FrameView const &b) { uyvyFrame<simd::abi::scalar>(a, b); }
void scalarRgba(ConstFrameView const &a, FrameView const &b) { rgbaFrame<simd::abi::scalar>(a, b); }
} // namespace lutils::image::simd_detail
