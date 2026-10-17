#include <array>
#include <iostream>
#include <lutils/image/SimdConversion.hpp>
#include <lutils/simd.hpp>
#include <stdexcept>

namespace s = lutils::simd;
namespace im = lutils::image;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
int main() {
    try {
        // Compile-time ABI for a short algorithm; runtime dispatch is shown by the image call.
#if defined(__aarch64__)
        using A = s::abi::neon;
#elif defined(__SSE2__)
        using A = s::abi::sse2;
#else
        using A = s::abi::scalar;
#endif
        using V = s::Batch<std::int32_t, A>;
        std::array<std::int32_t, 11> input{-5, 2, 0, 3, -1, 4, 7, -9, 8, 1, -3}, output{};
        auto count = s::copy_if<std::int32_t, A>(input.data(), input.size(), output.data(),
                                                 [](V values) { return values > V{0}; });
        std::cout << "positive values:";
        for (std::size_t i = 0; i < count; ++i)
            std::cout << ' ' << output[i];
        std::cout << '\n';
        im::FrameDesc desc{2,
                           2,
                           im::uyvy422(),
                           {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                            im::ChromaLocation::Cosited, im::Primaries::Bt601_625,
                            im::Transfer::Bt709}};
        auto source = take(im::HostFrame::create(desc));
        desc.format = im::nv12();
        desc.color.vertical = im::ChromaLocation::Midpoint;
        auto destination = take(im::HostFrame::create(desc));
        std::array<std::uint8_t, 8> raw{10, 20, 30, 40, 12, 22, 32, 42};
        auto in = source.view(), out = destination.view();
        for (std::size_t y = 0; y < 2; ++y)
            std::memcpy(im::row(in.planes[0], y), raw.data() + 4 * y, 4);
        auto result = im::convertUyvyToNv12(im::readOnly(in), out);
        if (!result)
            throw std::runtime_error(result.error().message);
        std::cout << "NV12 UV: " << std::to_integer<unsigned>(im::row(out.planes[1], 0)[0]) << ' '
                  << std::to_integer<unsigned>(im::row(out.planes[1], 0)[1]) << '\n';
        auto rgbDesc = out.desc;
        rgbDesc.format = im::rgba8();
        rgbDesc.color.matrix = im::Matrix::Identity;
        rgbDesc.color.range = im::Range::Full;
        rgbDesc.color.horizontal = im::ChromaLocation::Unknown;
        rgbDesc.color.vertical = im::ChromaLocation::Unknown;
        auto rgb = take(im::HostFrame::create(rgbDesc));
        auto plan = take(im::CpuConversionPlan::prepare(out.desc, rgbDesc));
        result = plan.run(im::readOnly(out), rgb.view());
        if (!result)
            throw std::runtime_error(result.error().message);
        std::cout << "RGBA first pixel:";
        for (std::size_t i = 0; i < 4; ++i)
            std::cout << ' ' << std::to_integer<unsigned>(im::row(rgb.view().planes[0], 0)[i]);
        std::cout << '\n';
        return 0;
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
