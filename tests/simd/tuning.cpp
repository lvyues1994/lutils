#include "../../image/simd/src/Tuning.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace im = lutils::image;
namespace s = lutils::simd;
namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("CPU tuning contract failed");
}
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void choose() {
    auto timing = [](s::Target target, double ns) {
        return im::CpuTargetTiming{target, {ns, ns, ns}, ns};
    };
    auto pick = [&](std::vector<im::CpuTargetTiming> const &values) {
        return im::simd_detail::chooseTiming(values, s::Target::Avx2);
    };
    check(pick({}) == s::Target::Avx2);
    check(pick({timing(s::Target::Scalar, 1)}) == s::Target::Avx2);
    check(pick({timing(s::Target::Avx2, 100), timing(s::Target::Scalar, 96)}) == s::Target::Avx2);
    check(pick({timing(s::Target::Avx2, 100), timing(s::Target::Scalar, 95)}) == s::Target::Scalar);
    check(pick({timing(s::Target::Avx2, 100), timing(s::Target::Scalar, 100)}) == s::Target::Avx2);
    check(pick({timing(s::Target::Avx2, 100), timing(s::Target::Scalar, NAN)}) == s::Target::Avx2);
    check(pick({timing(s::Target::Avx2, 100), timing(s::Target::Scalar, -1)}) == s::Target::Avx2);
    auto insufficient = timing(s::Target::Scalar, 1);
    insufficient.nanoseconds.resize(2);
    check(pick({timing(s::Target::Avx2, 100), insufficient}) == s::Target::Avx2);
}
void images() {
    for (auto width : {2u, 14u, 16u, 30u, 32u, 34u})
        for (bool rgba : {false, true}) {
            im::FrameDesc desc{width,
                               5,
                               rgba ? im::nv12() : im::uyvy422(),
                               {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                                rgba ? im::ChromaLocation::Midpoint : im::ChromaLocation::Cosited}};
            auto dest = desc;
            dest.format = rgba ? im::rgba8() : im::nv12();
            if (rgba)
                dest.color = {im::Matrix::Identity, im::Range::Full};
            else
                dest.color.vertical = im::ChromaLocation::Midpoint;
            auto in = take(im::HostFrame::create(desc, 64));
            auto out = take(im::HostFrame::create(dest, 64));
            auto ref = take(im::HostFrame::create(dest, 64));
            auto view = in.view();
            auto geometry = take(im::geometry(desc));
            for (std::size_t p = 0; p < geometry.size(); ++p)
                for (std::size_t y = 0; y < geometry[p].rows; ++y)
                    for (std::size_t x = 0; x < geometry[p].rowBytes; ++x)
                        im::row(view.planes[p], y)[x] = std::byte((17 * x + 37 * y) & 255);
            auto input = im::readOnly(view);
            auto output = out.view();
            auto baseline = take(im::CpuConversionPlan::prepare(desc, dest));
            check(width >= 16 || baseline.target() == s::Target::Scalar);
            check(width >= 32 || baseline.target() != s::Target::Avx2);
            for (auto target : {s::Target::Sse2, s::Target::Avx2, s::Target::Neon}) {
                auto explicitPlan = im::CpuConversionPlan::prepare(desc, dest, target);
                if (explicitPlan)
                    check(explicitPlan.value().target() == target);
            }
            auto scalar = take(im::CpuConversionPlan::prepare(desc, dest, s::Target::Scalar));
            check(bool(scalar.run(input, ref.view())));
            auto tuned = take(im::tuneCpuConversion(input, output));
            check(s::supported(tuned.plan.target()) && tuned.elapsed.count() > 0);
            auto count = tuned.timings.front().nanoseconds.size();
            for (auto const &t : tuned.timings)
                check(t.nanoseconds.size() == count && count <= 5);
            auto outGeometry = take(im::geometry(dest));
            auto reference = ref.view();
            for (std::size_t p = 0; p < outGeometry.size(); ++p)
                for (std::size_t y = 0; y < outGeometry[p].rows; ++y)
                    check(std::equal(im::row(output.planes[p], y),
                                     im::row(output.planes[p], y) + outGeometry[p].rowBytes,
                                     im::row(reference.planes[p], y)));
            check(!im::tuneCpuConversion(input, output, {std::chrono::milliseconds{0}}));
            check(!im::tuneCpuConversion(input, output, {std::chrono::seconds{11}}));
            auto bad = output;
            bad.planes[0].capacity = 0;
            check(!im::tuneCpuConversion(input, bad));
            auto limited =
                take(im::tuneCpuConversion(input, output, {std::chrono::milliseconds{1}}));
            check(s::supported(limited.plan.target()));
        }
}
} // namespace
int main() {
    try {
        choose();
        images();
        std::cout << "CPU tuning contracts passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
