#include "Rows.hpp"
#include <lutils/image/SimdConversion.hpp>

namespace lutils::image {
namespace {
bool overlaps(std::byte const *a, std::size_t aSize, std::byte const *b, std::size_t bSize) {
    auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < aSize : x - y < bSize;
}
simd::DispatchTable<simd_detail::Kernel> implementations(bool rgba) {
    simd::DispatchTable<simd_detail::Kernel> table;
    table.scalar = rgba ? simd_detail::scalarRgba : simd_detail::scalarUyvy;
#if defined(LUTILS_SIMD_X86)
    table.sse2 = rgba ? simd_detail::sse2Rgba : simd_detail::sse2Uyvy;
    table.avx2 = rgba ? simd_detail::avx2Rgba : simd_detail::avx2Uyvy;
#elif defined(LUTILS_SIMD_ARM64)
    table.neon = rgba ? simd_detail::neonRgba : simd_detail::neonUyvy;
#endif
    return table;
}
} // namespace
CpuConversionPlan::CpuConversionPlan(FrameDesc source, FrameDesc destination, Kernel kernel,
                                     simd::Target target)
    : source_(std::move(source)), destination_(std::move(destination)), kernel_(kernel),
      target_(target) {}
Result<CpuConversionPlan> CpuConversionPlan::prepare(FrameDesc a, FrameDesc b,
                                                     simd::Target target) {
    auto input = geometry(a), output = geometry(b);
    if (!input)
        return input.error();
    if (!output)
        return output.error();
    if (a.width != b.width || a.height != b.height)
        return Error{ErrorCode::InvalidArgument, "CPU conversion dimensions differ"};
    if (a.scan != Scan::Progressive || b.scan != Scan::Progressive)
        return Error{ErrorCode::Unsupported, "CPU conversion requires progressive frames"};
    bool const rgba = a.format == nv12() && b.format == rgba8();
    auto expectedColor = a.color;
    if (a.format == uyvy422() && b.format == nv12()) {
        expectedColor.vertical = ChromaLocation::Midpoint;
        if (a.color.vertical != ChromaLocation::Cosited ||
            a.color.horizontal == ChromaLocation::Unknown || !(b.color == expectedColor))
            return Error{ErrorCode::Unsupported,
                         "UYVY to NV12 requires known horizontal siting, "
                         "cosited input rows, midpoint output rows and unchanged color metadata"};
    } else if (rgba) {
        expectedColor.matrix = Matrix::Identity;
        expectedColor.range = Range::Full;
        expectedColor.horizontal = ChromaLocation::Unknown;
        expectedColor.vertical = ChromaLocation::Unknown;
        if (a.color.matrix != Matrix::Bt601 || a.color.range != Range::Limited ||
            a.color.horizontal == ChromaLocation::Unknown ||
            a.color.vertical != ChromaLocation::Midpoint || !(b.color == expectedColor))
            return Error{ErrorCode::Unsupported,
                         "NV12 to RGBA requires BT.601 limited, known "
                         "horizontal/midpoint vertical siting and full-range RGB output"};
    } else
        return Error{ErrorCode::Unsupported, "no CPU SIMD conversion for these formats"};
    auto table = implementations(rgba);
    if (target == simd::Target::Automatic) {
        // Both kernels process one byte batch of pixels per iteration. A wider
        // implementation cannot enter its vector loop on a shorter row.
        if (a.width < 32)
            table.avx2 = nullptr;
        if (a.width < 16)
            table.sse2 = table.neon = nullptr;
    }
    auto selected = simd::resolve(table, target);
    if (!selected)
        return Error{ErrorCode::Unsupported, "SIMD target is unavailable"};
    return CpuConversionPlan{std::move(a), std::move(b), selected->function, selected->target};
}
Result<void> CpuConversionPlan::run(ConstFrameView const &source,
                                    FrameView const &destination) const {
    if (!(source.desc == source_) || !(destination.desc == destination_))
        return Error{ErrorCode::InvalidArgument, "frames do not match prepared CPU conversion"};
    auto valid = validate(source);
    if (!valid)
        return valid;
    valid = validate(destination);
    if (!valid)
        return valid;
    for (auto const &input : source.planes)
        for (auto const &output : destination.planes)
            if (overlaps(input.base, input.capacity, output.base, output.capacity))
                return Error{ErrorCode::InvalidArgument, "CPU conversion input and output overlap"};
    kernel_(source, destination);
    return {};
}
Result<void> convertUyvyToNv12(ConstFrameView const &source, FrameView const &destination,
                               simd::Target target) {
    if (source.desc.format != uyvy422() || destination.desc.format != nv12())
        return Error{ErrorCode::Unsupported, "conversion requires UYVY422 and NV12"};
    auto plan = CpuConversionPlan::prepare(source.desc, destination.desc, target);
    return plan ? plan.value().run(source, destination) : Result<void>{plan.error()};
}
Result<void> convertNv12ToRgba(ConstFrameView const &source, FrameView const &destination,
                               simd::Target target) {
    if (source.desc.format != nv12() || destination.desc.format != rgba8())
        return Error{ErrorCode::Unsupported, "conversion requires NV12 and RGBA8"};
    auto plan = CpuConversionPlan::prepare(source.desc, destination.desc, target);
    return plan ? plan.value().run(source, destination) : Result<void>{plan.error()};
}
} // namespace lutils::image
