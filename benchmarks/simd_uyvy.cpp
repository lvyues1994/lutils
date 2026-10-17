#include <algorithm>
#include <chrono>
#include <iostream>
#include <lutils/image/SimdConversion.hpp>
#include <stdexcept>
#include <vector>

namespace im = lutils::image;
namespace s = lutils::simd;
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(lutils::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
int main() {
    try {
        // Fixed bounded working set, one thread, no device or filesystem access.
        im::FrameDesc desc{1920,
                           1536,
                           im::uyvy422(),
                           {im::Matrix::Bt709, im::Range::Limited, im::ChromaLocation::Cosited,
                            im::ChromaLocation::Cosited, im::Primaries::Bt709,
                            im::Transfer::Bt709}};
        auto input = take(im::HostFrame::create(desc, 64));
        desc.format = im::nv12();
        desc.color.vertical = im::ChromaLocation::Midpoint;
        auto reference = take(im::HostFrame::create(desc, 64));
        auto output = take(im::HostFrame::create(desc, 64));
        auto src = input.view(), expected = reference.view(), dst = output.view();
        for (std::size_t y = 0; y < desc.height; ++y)
            for (std::size_t x = 0; x < 2 * desc.width; ++x)
                im::row(src.planes[0], y)[x] = static_cast<std::byte>((x * 23 + y * 131) % 256);
        auto source = im::readOnly(src);
        take(im::convertUyvyToNv12(source, expected, s::Target::Scalar));
        for (auto target : {s::Target::Scalar, s::Target::Sse2, s::Target::Avx2, s::Target::Neon}) {
            if (!s::supported(target))
                continue;
            take(im::convertUyvyToNv12(source, dst, target));
            std::vector<double> samples;
            for (int i = 0; i < 9; ++i) {
                auto start = std::chrono::steady_clock::now();
                take(im::convertUyvyToNv12(source, dst, target));
                samples.push_back(std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count());
            }
            for (std::size_t plane = 0; plane < 2; ++plane)
                for (std::size_t y = 0; y < (plane ? (desc.height + 1) / 2 : desc.height); ++y)
                    if (!std::equal(im::row(expected.planes[plane], y),
                                    im::row(expected.planes[plane], y) + desc.width,
                                    im::row(dst.planes[plane], y)))
                        throw std::runtime_error("conversion mismatch");
            std::sort(samples.begin(), samples.end());
            std::cout << s::target_name(target) << " median_ms=" << samples[4]
                      << " min_ms=" << samples.front() << " max_ms=" << samples.back() << '\n';
        }
        return 0;
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
