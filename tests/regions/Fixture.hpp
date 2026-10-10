#pragma once
#include <algorithm>
#include <iostream>
#include <lutils/image/ImageResource.hpp>
#include <stdexcept>

namespace regions_test {
namespace im = lutils::image;
namespace co = lutils::compute;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
inline void ok(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
inline void check(bool condition) {
    if (!condition)
        throw std::runtime_error("region assertion failed");
}
struct PlaneSpec {
    unsigned width, height, bytes;
    std::vector<unsigned> background;
};
struct Spec {
    im::FormatDesc format;
    std::vector<PlaneSpec> planes;
    im::Background color;
};
inline std::vector<Spec> formats() {
    im::Background yuv{{im::Component::Y, 16}, {im::Component::Cb, 128}, {im::Component::Cr, 129}};
    return {{im::rgba8(),
             {{1, 1, 4, {17, 35, 53, 71}}},
             {{im::Component::R, 17},
              {im::Component::G, 35},
              {im::Component::B, 53},
              {im::Component::A, 71}}},
            {im::uyvy422(), {{2, 1, 4, {128, 16, 129, 16}}}, yuv},
            {im::yuyv422(), {{2, 1, 4, {16, 128, 16, 129}}}, yuv},
            {im::nv12(), {{1, 1, 1, {16}}, {2, 2, 2, {128, 129}}}, yuv},
            {im::i420(), {{1, 1, 1, {16}}, {2, 2, 1, {128}}, {2, 2, 1, {129}}}, yuv}};
}
struct Storage {
    im::FrameDesc desc;
    std::vector<std::byte> bytes;
    std::vector<im::PlaneLayout> planes;
    Storage(Spec const &spec, unsigned width, unsigned height, std::size_t offset, unsigned padding,
            bool reversed = false)
        : desc{width, height, spec.format, {}, im::Scan::Progressive} {
        auto cursor = offset;
        for (auto const &s : spec.planes) {
            auto rowBytes = std::size_t{(width + s.width - 1) / s.width} * s.bytes;
            auto rows = (height + s.height - 1) / s.height;
            auto stride = rowBytes + padding;
            auto step = static_cast<std::ptrdiff_t>(stride);
            planes.push_back(
                {0, cursor + (reversed ? (rows - 1) * stride : 0), reversed ? -step : step});
            cursor += stride * rows + 1; // Adjacent planes can share a native word.
        }
        bytes.assign((cursor + 7) / 4 * 4, std::byte{0xA5});
    }
    im::FrameView view() {
        im::FrameView result{desc, {}};
        for (auto const &p : planes)
            result.planes.push_back({bytes.data(), bytes.size(), p.offset, p.stride});
        return result;
    }
    std::size_t at(std::size_t plane, std::size_t x, std::size_t y) const {
        auto const &p = planes[plane];
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(p.offset) +
                                        static_cast<std::ptrdiff_t>(y) * p.stride) +
               x;
    }
    void fill(Spec const &spec) {
        for (std::size_t p = 0; p < planes.size(); ++p) {
            auto s = spec.planes[p];
            for (std::size_t y = 0; y < (desc.height + s.height - 1) / s.height; ++y)
                for (std::size_t x = 0;
                     x < std::size_t{(desc.width + s.width - 1) / s.width} * s.bytes; ++x)
                    bytes[at(p, x, y)] = static_cast<std::byte>((p * 61 + y * 23 + x * 7) % 256);
        }
    }
    im::ImageResource host() { return take(im::ImageResource::borrow(view())); }
    im::ImageResource resident(co::Device &device) const {
        auto buffer = take(device.createBuffer(bytes.size() / 4));
        std::vector<co::Word> words(bytes.size() / 4);
        for (std::size_t b = 0; b < bytes.size(); ++b)
            words[b / 4] |= std::to_integer<co::Word>(bytes[b]) << ((b % 4) * 8);
        ok(device.upload(buffer, words));
        return take(im::ImageResource::linear(desc, {take(co::bufferMemory(buffer))}, planes));
    }
};
inline std::vector<std::byte> expected(Spec const &spec, Storage const &src, Storage const &dst,
                                       im::Rectangle rectangle, im::Point position) {
    auto result = dst.bytes;
    // Independent oracle: explicit plane subsampling and byte patterns above,
    // no production region plan, format bit encoder or GPU addressing helper.
    for (std::size_t p = 0; p < spec.planes.size(); ++p) {
        auto const &s = spec.planes[p];
        auto left = std::size_t{position.x / s.width} * s.bytes;
        auto top = position.y / s.height;
        auto width = std::size_t{rectangle.width / s.width} * s.bytes;
        auto height = rectangle.height / s.height;
        for (std::size_t y = 0; y < (dst.desc.height + s.height - 1) / s.height; ++y)
            for (std::size_t x = 0;
                 x < std::size_t{(dst.desc.width + s.width - 1) / s.width} * s.bytes; ++x) {
                auto value = static_cast<std::byte>(s.background[x % s.background.size()]);
                if (x >= left && x - left < width && y >= top && y - top < height)
                    value =
                        src.bytes[src.at(p, std::size_t{rectangle.x / s.width} * s.bytes + x - left,
                                         rectangle.y / s.height + y - top)];
                result[dst.at(p, x, y)] = value;
            }
    }
    return result;
}
inline std::vector<std::byte> download(co::Device &device, im::ImageResource const &image) {
    auto words = take(device.download(image.objects()[0]->residentBuffer()));
    std::vector<std::byte> bytes(words.size() * 4);
    for (std::size_t b = 0; b < bytes.size(); ++b)
        bytes[b] = static_cast<std::byte>((words[b / 4] >> ((b % 4) * 8)) & 255);
    return bytes;
}
} // namespace regions_test
