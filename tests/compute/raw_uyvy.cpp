#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <lutils/image/Conversion.hpp>
#include <stdexcept>
#include <string>

namespace co = lutils::compute;
namespace im = lutils::image;
namespace fs = std::filesystem;
namespace {
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
    if (!value || text[consumed] != '\0' || value > UINT32_MAX)
        throw std::runtime_error{"invalid frame dimension"};
    return static_cast<std::uint32_t>(value);
}
struct Difference {
    std::size_t yBytes = 0;
    std::size_t uvBytes = 0;
    unsigned maxError = 0;
};
// Independent byte-level oracle; no kernel word packing or extraction helpers.
Difference compare(im::ConstFrameView const &input, im::ConstFrameView const &output) {
    Difference result;
    auto sample = [&](std::byte actual, unsigned expected, std::size_t &count) {
        auto value = std::to_integer<unsigned>(actual);
        auto error = value > expected ? value - expected : expected - value;
        if (error)
            ++count;
        result.maxError = std::max(result.maxError, error);
    };
    for (std::size_t y = 0; y < input.desc.height; ++y) {
        auto source = im::row(input.planes[0], y);
        auto target = im::row(output.planes[0], y);
        for (std::size_t x = 0; x < input.desc.width; ++x)
            sample(target[x], std::to_integer<unsigned>(source[2 * x + 1]), result.yBytes);
    }
    auto uvRows = input.desc.height / 2u + input.desc.height % 2u;
    for (std::size_t y = 0; y < uvRows; ++y) {
        auto top = im::row(input.planes[0], y * 2);
        auto bottom =
            im::row(input.planes[0], std::min(y * 2 + 1, std::size_t{input.desc.height - 1}));
        auto target = im::row(output.planes[1], y);
        for (std::size_t x = 0; x < input.desc.width; ++x) {
            auto expected = (std::to_integer<unsigned>(top[2 * x]) +
                             std::to_integer<unsigned>(bottom[2 * x]) + 1u) /
                            2u;
            sample(target[x], expected, result.uvBytes);
        }
    }
    return result;
}
void read(fs::path const &path, im::FrameView const &frame) {
    auto const &plane = frame.planes[0];
    if (fs::file_size(path) != plane.capacity)
        throw std::runtime_error{"input size does not match tightly packed UYVY: " + path.string()};
    if (plane.capacity > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error{"input is too large for stream I/O"};
    std::ifstream in{path, std::ios::binary};
    in.read(reinterpret_cast<char *>(plane.base), static_cast<std::streamsize>(plane.capacity));
    if (!in)
        throw std::runtime_error{"cannot read " + path.string()};
}
void write(fs::path const &path, im::ConstFrameView const &frame) {
    if (fs::exists(path))
        throw std::runtime_error{"output already exists: " + path.string()};
    auto shapes = take(im::geometry(frame.desc));
    std::ofstream out{path, std::ios::binary};
    for (std::size_t p = 0; p < frame.planes.size(); ++p)
        for (std::size_t y = 0; y < shapes[p].rows; ++y)
            out.write(reinterpret_cast<char const *>(im::row(frame.planes[p], y)),
                      static_cast<std::streamsize>(shapes[p].rowBytes));
    out.close();
    if (!out)
        throw std::runtime_error{"cannot write " + path.string()};
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 6)
            throw std::runtime_error{
                "usage: uyvy_nv12_test cpu|vulkan width height output_dir input.uyvy [...]"};
        im::FrameDesc desc{
            dimension(argv[2]), dimension(argv[3]), im::uyvy422(), {}, im::Scan::Progressive};
        // Test policy: progressive rows, horizontal siting preserved, vertical box filter.
        // Matrix/range/primaries/transfer remain unknown and never change sample values.
        desc.color.horizontal = im::ChromaLocation::Cosited;
        desc.color.vertical = im::ChromaLocation::Cosited;
        auto outputDesc = desc;
        outputDesc.format = im::nv12();
        outputDesc.color.vertical = im::ChromaLocation::Midpoint;
        auto shapes = take(im::geometry(desc));
        if (shapes[0].rows > std::numeric_limits<std::size_t>::max() / shapes[0].rowBytes)
            throw std::runtime_error{"input byte count overflows"};
        auto inputBytes = shapes[0].rowBytes * shapes[0].rows;
        for (int i = 5; i < argc; ++i)
            if (fs::file_size(argv[i]) != inputBytes)
                throw std::runtime_error{"input size does not match tightly packed UYVY: " +
                                         std::string{argv[i]}};
        std::unique_ptr<co::Device> device;
        if (std::string{argv[1]} == "cpu")
            device = take(co::createCpuDevice());
#ifdef LUTILS_TEST_VULKAN
        else if (std::string{argv[1]} == "vulkan")
            device = take(co::createVulkanDevice());
#endif
        else
            throw std::runtime_error{"requested backend is not available in this build"};
        auto source = take(im::HostFrame::create(desc));
        auto output = take(im::HostFrame::create(outputDesc));
        auto deviceSource = take(im::DeviceFrame::create(*device, desc));
        auto deviceOutput = take(im::DeviceFrame::create(*device, outputDesc));
        auto plan = take(im::ConversionPlan::prepare(*device, desc, outputDesc));
        co::CommandList commands;
        check(plan.record(commands, deviceSource, deviceOutput));
        fs::path directory{argv[4]};
        fs::create_directories(directory);
        std::cout << "device=" << device->info().name << '\n'
                  << "frame\ty_mismatch_bytes\tuv_mismatch_bytes\tmax_error\n";
        for (int i = 5; i < argc; ++i) {
            fs::path path{argv[i]};
            read(path, source.view());
            check(im::upload(*device, im::readOnly(source.view()), deviceSource));
            auto done = take(device->submit(commands));
            check(done->wait());
            check(im::download(*device, deviceOutput, output.view()));
            auto difference = compare(im::readOnly(source.view()), im::readOnly(output.view()));
            std::cout << path.filename().string() << '\t' << difference.yBytes << '\t'
                      << difference.uvBytes << '\t' << difference.maxError << std::endl;
            if (difference.yBytes || difference.uvBytes)
                throw std::runtime_error{"conversion differs from byte-level reference"};
            auto destination = directory / path.filename();
            destination.replace_extension(".nv12");
            write(destination, im::readOnly(output.view()));
        }
        std::cout << "PASS frames=" << argc - 5 << '\n';
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
