#include <iostream>
#include <lutils/image/Conversion.hpp>
#include <stdexcept>
#include <string>
#ifdef LUTILS_EXAMPLE_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif

namespace co = lutils::compute;
namespace im = lutils::image;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error{result.error().message};
    return std::move(result).value();
}
void check(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error{result.error().message};
}
std::uint32_t dimension(char const *text) {
    std::size_t consumed = 0;
    auto value = std::stoull(text, &consumed);
    if (text[consumed] != '\0' || value > UINT32_MAX)
        throw std::runtime_error{"invalid dimension"};
    return static_cast<std::uint32_t>(value);
}
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 4)
            throw std::runtime_error{"usage: image_compute cpu|vulkan [width height]"};
        auto width = argc == 4 ? dimension(argv[2]) : 640u;
        auto height = argc == 4 ? dimension(argv[3]) : 480u;
        std::unique_ptr<co::Device> device;
        if (std::string{argv[1]} == "cpu")
            device = take(co::createCpuDevice());
#ifdef LUTILS_EXAMPLE_VULKAN
        else if (std::string{argv[1]} == "vulkan") {
            co::VulkanOptions options;
            options.deviceLocal = true;
            device = take(co::createVulkanDevice(options));
        }
#endif
        else
            throw std::runtime_error{"requested backend is not available in this build"};
        im::FrameDesc input{width,
                            height,
                            im::yuyv422(),
                            {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                             im::ChromaLocation::Cosited, im::Primaries::Bt601_625,
                             im::Transfer::Bt709},
                            im::Scan::Progressive};
        auto middle = input;
        middle.format = im::nv12();
        middle.color.vertical = im::ChromaLocation::Midpoint;
        auto output = input;
        output.format = im::rgba8();
        output.color.matrix = im::Matrix::Identity;
        output.color.range = im::Range::Full;
        output.color.horizontal = im::ChromaLocation::Unknown;
        output.color.vertical = im::ChromaLocation::Unknown;
        auto host = take(im::HostFrame::create(input));
        auto result = take(im::HostFrame::create(output));
        auto view = host.view();
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x) {
                auto row = im::row(view.planes[0], y);
                row[x * 2] = static_cast<std::byte>(16u + (x + y) % 220u);
                row[x * 2 + 1] = std::byte{128};
            }
        auto source = take(im::DeviceFrame::create(*device, input));
        auto nv12 = take(im::DeviceFrame::create(*device, middle));
        auto rgba = take(im::DeviceFrame::create(*device, output));
        auto first = take(im::ConversionPlan::prepare(*device, input, middle));
        auto second = take(im::ConversionPlan::prepare(*device, middle, output));
        co::CommandList commands;
        check(im::recordUpload(commands, im::readOnly(view), source));
        check(first.record(commands, source, nv12));
        check(second.record(commands, nv12, rgba));
        auto readback = take(im::recordReadback(commands, rgba));
        auto done = take(device->submit(commands));
        // Other frames may be submitted here before collecting this frame.
        check(readback.copyTo(*done, result.view()));
        std::uint64_t checksum = 0;
        auto pixels = result.view().planes[0];
        for (std::size_t i = 0; i < pixels.capacity; ++i)
            checksum += std::to_integer<unsigned>(pixels.base[i]);
        std::cout << device->info().name << '\n'
                  << width << 'x' << height << " YUYV422 -> NV12 -> RGBA8; checksum=" << checksum
                  << '\n';
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
