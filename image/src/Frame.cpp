#include <algorithm>
#include <limits>
#include <lutils/image/Frame.hpp>
#include <map>
#include <tuple>

namespace lutils::image {
namespace {
Error invalid(char const *message) { return {ErrorCode::InvalidArgument, message}; }
Error overflow() { return {ErrorCode::Overflow, "image layout exceeds addressable range"}; }
bool same(SampleBits const &a, SampleBits const &b) {
    return std::tie(a.component, a.sampleX, a.sampleY, a.storageBit, a.valueBit, a.bitCount) ==
           std::tie(b.component, b.sampleX, b.sampleY, b.storageBit, b.valueBit, b.bitCount);
}
bool same(PlaneFormat const &a, PlaneFormat const &b) {
    return a.blockWidth == b.blockWidth && a.blockHeight == b.blockHeight &&
           a.blockBytes == b.blockBytes && a.samples.size() == b.samples.size() &&
           std::equal(a.samples.begin(), a.samples.end(), b.samples.begin(),
                      [](auto const &x, auto const &y) { return same(x, y); });
}
bool same(ComponentDesc const &a, ComponentDesc const &b) {
    return std::tie(a.component, a.stepX, a.stepY, a.valueBits, a.encoding) ==
           std::tie(b.component, b.stepX, b.stepY, b.valueBits, b.encoding);
}
Result<void> checkFormat(FormatDesc const &format) {
    if (format.planes.empty() || format.components.empty() || !format.widthMultiple ||
        !format.heightMultiple)
        return invalid("format requires planes and nonzero dimension multiples");
    for (std::size_t i = 0; i < format.components.size(); ++i) {
        auto const &c = format.components[i];
        if (c.component < Component::Y || c.component > Component::A ||
            c.encoding < SampleEncoding::Unsigned || c.encoding > SampleEncoding::Float ||
            !c.stepX || !c.stepY || !c.valueBits || c.valueBits > 32 ||
            (c.encoding == SampleEncoding::Float && c.valueBits != 16 && c.valueBits != 32))
            return invalid("unsupported component step or value representation");
        for (std::size_t j = 0; j < i; ++j)
            if (format.components[j].component == c.component)
                return invalid("duplicate component");
    }
    for (auto const &p : format.planes) {
        if (!p.blockWidth || !p.blockHeight || !p.blockBytes || p.samples.empty())
            return invalid("plane requires nonempty blocks and sample mappings");
        for (auto const &s : p.samples) {
            auto component =
                std::find_if(format.components.begin(), format.components.end(),
                             [&](auto const &c) { return c.component == s.component; });
            if (component == format.components.end() || p.blockWidth % component->stepX ||
                p.blockHeight % component->stepY || s.sampleX >= p.blockWidth / component->stepX ||
                s.sampleY >= p.blockHeight / component->stepY ||
                s.valueBit >= component->valueBits ||
                s.bitCount > component->valueBits - s.valueBit)
                return invalid("sample does not match its component lattice or bit width");
            if (s.sampleX >= p.blockWidth || s.sampleY >= p.blockHeight || !s.bitCount ||
                s.bitCount > 32 || s.valueBit > 32 - s.bitCount ||
                std::uint64_t{s.storageBit} + s.bitCount > std::uint64_t{p.blockBytes} * 8)
                return invalid("sample bit mapping is outside its block or value");
        }
        for (std::size_t i = 0; i < p.samples.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                auto const &a = p.samples[i];
                auto const &b = p.samples[j];
                if (std::uint64_t{a.storageBit} < std::uint64_t{b.storageBit} + b.bitCount &&
                    std::uint64_t{b.storageBit} < std::uint64_t{a.storageBit} + a.bitCount)
                    return invalid("sample storage bit ranges overlap");
                if (a.component == b.component && a.sampleX == b.sampleX &&
                    a.sampleY == b.sampleY && a.valueBit < b.valueBit + b.bitCount &&
                    b.valueBit < a.valueBit + a.bitCount)
                    return invalid("sample value bit ranges overlap");
            }
        }
    }
    for (auto const &c : format.components) {
        std::size_t owningPlanes = 0;
        for (auto const &p : format.planes) {
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> coverage;
            for (auto const &s : p.samples)
                if (s.component == c.component)
                    coverage[{s.sampleX, s.sampleY}] |= ((std::uint64_t{1} << s.bitCount) - 1)
                                                        << s.valueBit;
            if (coverage.empty())
                continue;
            ++owningPlanes;
            auto expected = (std::uint64_t{1} << c.valueBits) - 1;
            for (auto const &item : coverage)
                if (item.second != expected)
                    return invalid("sample value bits are incomplete");
            if (coverage.size() !=
                std::uint64_t{p.blockWidth / c.stepX} * (p.blockHeight / c.stepY))
                return invalid("component samples do not cover the block");
        }
        if (owningPlanes != 1)
            return invalid("component must belong to exactly one plane");
    }
    return {};
}
std::size_t strideMagnitude(std::ptrdiff_t stride) {
    return stride < 0 ? static_cast<std::size_t>(-(stride + 1)) + 1
                      : static_cast<std::size_t>(stride);
}
Result<void> checkPlane(ConstPlaneView const &p, PlaneGeometry g) {
    if (p.row0 > p.capacity)
        return invalid("row zero is outside plane allocation");
    if (!g.rows || !g.rowBytes)
        return {};
    if (!p.base)
        return invalid("nonempty plane has no storage");
    if (p.capacity >
        std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(p.base))
        return invalid("plane address range overflows");
    auto const step = strideMagnitude(p.stride);
    if (g.rows > 1 && step < g.rowBytes)
        return invalid("plane rows overlap");
    auto const limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (p.capacity > limit || (g.rows > 1 && step > limit / (g.rows - 1)))
        return overflow();
    auto const travel = (g.rows - 1) * step;
    if (p.stride < 0 && travel > p.row0)
        return invalid("negative stride precedes allocation");
    auto const endRow = p.stride < 0 ? p.row0 : p.row0 + travel;
    if (endRow < p.row0 || endRow > p.capacity || g.rowBytes > p.capacity - endRow)
        return invalid("plane capacity does not cover all active rows");
    return {};
}
} // namespace

bool operator==(FormatDesc const &a, FormatDesc const &b) {
    return a.widthMultiple == b.widthMultiple && a.heightMultiple == b.heightMultiple &&
           a.planes.size() == b.planes.size() && a.components.size() == b.components.size() &&
           std::equal(a.components.begin(), a.components.end(), b.components.begin(),
                      [](auto const &x, auto const &y) { return same(x, y); }) &&
           std::equal(a.planes.begin(), a.planes.end(), b.planes.begin(),
                      [](auto const &x, auto const &y) { return same(x, y); });
}
bool operator==(ColorSpec const &a, ColorSpec const &b) {
    return std::tie(a.matrix, a.range, a.horizontal, a.vertical, a.primaries, a.transfer) ==
           std::tie(b.matrix, b.range, b.horizontal, b.vertical, b.primaries, b.transfer);
}
bool operator==(FrameDesc const &a, FrameDesc const &b) {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.color == b.color && a.scan == b.scan;
}
FormatDesc rgba8() {
    return {"RGBA8",
            {{1,
              1,
              4,
              {{Component::R, 0, 0, 0, 0, 8},
               {Component::G, 0, 0, 8, 0, 8},
               {Component::B, 0, 0, 16, 0, 8},
               {Component::A, 0, 0, 24, 0, 8}}}},
            1,
            1,
            {{Component::R}, {Component::G}, {Component::B}, {Component::A}}};
}
FormatDesc yuyv422() {
    return {"YUYV422",
            {{2,
              1,
              4,
              {{Component::Y, 0, 0, 0, 0, 8},
               {Component::Cb, 0, 0, 8, 0, 8},
               {Component::Y, 1, 0, 16, 0, 8},
               {Component::Cr, 0, 0, 24, 0, 8}}}},
            2,
            1,
            {{Component::Y}, {Component::Cb, 2, 1}, {Component::Cr, 2, 1}}};
}
FormatDesc uyvy422() {
    return {"UYVY422",
            {{2,
              1,
              4,
              {{Component::Cb, 0, 0, 0, 0, 8},
               {Component::Y, 0, 0, 8, 0, 8},
               {Component::Cr, 0, 0, 16, 0, 8},
               {Component::Y, 1, 0, 24, 0, 8}}}},
            2,
            1,
            {{Component::Y}, {Component::Cb, 2, 1}, {Component::Cr, 2, 1}}};
}
FormatDesc nv12() {
    return {"NV12",
            {{1, 1, 1, {{Component::Y, 0, 0, 0, 0, 8}}},
             {2, 2, 2, {{Component::Cb, 0, 0, 0, 0, 8}, {Component::Cr, 0, 0, 8, 0, 8}}}},
            1,
            1,
            {{Component::Y}, {Component::Cb, 2, 2}, {Component::Cr, 2, 2}}};
}
FormatDesc i420() {
    return {"I420",
            {{1, 1, 1, {{Component::Y, 0, 0, 0, 0, 8}}},
             {2, 2, 1, {{Component::Cb, 0, 0, 0, 0, 8}}},
             {2, 2, 1, {{Component::Cr, 0, 0, 0, 0, 8}}}},
            1,
            1,
            {{Component::Y}, {Component::Cb, 2, 2}, {Component::Cr, 2, 2}}};
}
Result<std::vector<PlaneGeometry>> geometry(FrameDesc const &desc) {
    if (desc.color.matrix < Matrix::Unknown || desc.color.matrix > Matrix::Bt2020 ||
        desc.color.range < Range::Unknown || desc.color.range > Range::Limited ||
        desc.color.horizontal < ChromaLocation::Unknown ||
        desc.color.horizontal > ChromaLocation::Midpoint ||
        desc.color.vertical < ChromaLocation::Unknown ||
        desc.color.vertical > ChromaLocation::Midpoint ||
        desc.color.primaries < Primaries::Unknown || desc.color.primaries > Primaries::Bt2020 ||
        desc.color.transfer < Transfer::Unknown || desc.color.transfer > Transfer::Linear ||
        (desc.scan != Scan::Progressive && desc.scan != Scan::Interlaced))
        return invalid("unknown frame metadata value");
    if ((!desc.width) != (!desc.height))
        return invalid("empty frames require both dimensions zero");
    auto valid = checkFormat(desc.format);
    if (!valid)
        return valid.error();
    if (desc.width % desc.format.widthMultiple || desc.height % desc.format.heightMultiple)
        return invalid("dimensions violate format block constraints");
    std::vector<PlaneGeometry> result;
    auto const limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    for (auto const &p : desc.format.planes) {
        auto const blocks = desc.width / p.blockWidth + (desc.width % p.blockWidth != 0 ? 1u : 0u);
        auto const rows =
            desc.height / p.blockHeight + (desc.height % p.blockHeight != 0 ? 1u : 0u);
        if (blocks > limit / p.blockBytes)
            return overflow();
        result.push_back({static_cast<std::size_t>(blocks) * p.blockBytes, rows});
    }
    return result;
}
ConstFrameView readOnly(FrameView const &view) {
    ConstFrameView result{view.desc, {}};
    for (auto const &p : view.planes)
        result.planes.push_back({p.base, p.capacity, p.row0, p.stride});
    return result;
}
Result<void> validate(ConstFrameView const &view) {
    auto shapes = geometry(view.desc);
    if (!shapes)
        return shapes.error();
    if (view.planes.size() != shapes.value().size())
        return invalid("plane count differs from format");
    for (std::size_t i = 0; i < view.planes.size(); ++i) {
        auto status = checkPlane(view.planes[i], shapes.value()[i]);
        if (!status)
            return status;
    }
    return {};
}
Result<void> validate(FrameView const &view) {
    auto valid = validate(readOnly(view));
    if (!valid)
        return valid;
    auto shapes = geometry(view.desc).value();
    auto interval = [&](std::size_t plane, std::size_t orderedRow) {
        auto const &p = view.planes[plane];
        auto y = p.stride < 0 ? shapes[plane].rows - 1 - orderedRow : orderedRow;
        auto start = reinterpret_cast<std::uintptr_t>(row(p, y));
        return std::pair<std::uintptr_t, std::uintptr_t>{start, start + shapes[plane].rowBytes};
    };
    for (std::size_t a = 0; a < view.planes.size(); ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            if (!shapes[a].rowBytes || !shapes[b].rowBytes || !shapes[a].rows || !shapes[b].rows)
                continue;
            if (interval(a, 0).first >= interval(b, shapes[b].rows - 1).second ||
                interval(b, 0).first >= interval(a, shapes[a].rows - 1).second)
                continue;
            std::size_t x = 0;
            std::size_t y = 0;
            while (x < shapes[a].rows && y < shapes[b].rows) {
                auto left = interval(a, x);
                auto right = interval(b, y);
                if (left.first < right.second && right.first < left.second)
                    return invalid("writable plane rows overlap");
                if (left.second <= right.first)
                    ++x;
                else
                    ++y;
            }
        }
    }
    return {};
}
std::byte const *row(ConstPlaneView const &p, std::size_t y) {
    auto const delta = y * strideMagnitude(p.stride);
    return p.base + (p.stride < 0 ? p.row0 - delta : p.row0 + delta);
}
std::byte *row(PlaneView const &p, std::size_t y) {
    auto const delta = y * strideMagnitude(p.stride);
    return p.base + (p.stride < 0 ? p.row0 - delta : p.row0 + delta);
}
Result<HostFrame> HostFrame::create(FrameDesc desc, std::size_t alignment) {
    auto shapes = geometry(desc);
    if (!shapes)
        return shapes.error();
    if (!alignment)
        return invalid("row alignment must be positive");
    HostFrame result;
    result.desc_ = std::move(desc);
    auto const limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    for (auto const &shape : shapes.value()) {
        auto const padding =
            shape.rowBytes % alignment == 0 ? 0 : alignment - shape.rowBytes % alignment;
        if (padding > limit - shape.rowBytes)
            return overflow();
        auto const stride = shape.rowBytes + padding;
        if (shape.rows && stride > limit / shape.rows)
            return overflow();
        result.strides_.push_back(stride);
        result.planes_.emplace_back(stride * shape.rows);
    }
    return result;
}
FrameView HostFrame::view() {
    FrameView result{desc_, {}};
    for (std::size_t i = 0; i < planes_.size(); ++i)
        result.planes.push_back(
            {planes_[i].data(), planes_[i].size(), 0, static_cast<std::ptrdiff_t>(strides_[i])});
    return result;
}
ConstFrameView HostFrame::view() const {
    ConstFrameView result{desc_, {}};
    for (std::size_t i = 0; i < planes_.size(); ++i)
        result.planes.push_back(
            {planes_[i].data(), planes_[i].size(), 0, static_cast<std::ptrdiff_t>(strides_[i])});
    return result;
}
} // namespace lutils::image
