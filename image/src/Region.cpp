#include <algorithm>
#include <cstring>
#include <lutils/image/Region.hpp>

namespace lutils::image {
namespace {
bool fits(Rectangle r, FrameDesc const &d) {
    return r.width && r.height && r.x <= d.width && r.y <= d.height && r.width <= d.width - r.x &&
           r.height <= d.height - r.y;
}
bool overlaps(ConstPlaneView const &a, PlaneView const &b) {
    auto x = reinterpret_cast<std::uintptr_t>(a.base);
    auto y = reinterpret_cast<std::uintptr_t>(b.base);
    return x <= y ? y - x < a.capacity : x - y < b.capacity;
}
} // namespace
Result<RegionPlan> RegionPlan::prepare(FrameDesc source, FrameDesc destination, Rectangle rectangle,
                                       Point position, std::optional<Background> background) {
    auto sg = geometry(source);
    if (!sg)
        return sg.error();
    auto dg = geometry(destination);
    if (!dg)
        return dg.error();
    if (source.format != destination.format || !(source.color == destination.color) ||
        source.scan != Scan::Progressive || destination.scan != Scan::Progressive)
        return Error{ErrorCode::Unsupported,
                     "regions require identical format/color and progressive images"};
    if (!fits(rectangle, source) ||
        !fits({position.x, position.y, rectangle.width, rectangle.height}, destination))
        return Error{ErrorCode::Bounds, "empty or out-of-bounds image region"};
    if (background) {
        if (background->size() != source.format.components.size())
            return Error{ErrorCode::InvalidArgument,
                         "background must specify each format component once"};
        for (auto const &component : source.format.components) {
            auto count = std::count_if(background->begin(), background->end(), [&](auto const &v) {
                return v.component == component.component;
            });
            if (count != 1)
                return Error{ErrorCode::InvalidArgument,
                             "missing or duplicate background component"};
            auto value = std::find_if(background->begin(), background->end(), [&](auto const &v) {
                             return v.component == component.component;
                         })->value;
            if (component.encoding != SampleEncoding::Unsigned || component.valueBits != 8)
                return Error{ErrorCode::Unsupported,
                             "background requires unsigned 8-bit components"};
            if (value > 255)
                return Error{ErrorCode::InvalidArgument,
                             "background component exceeds its encoded range"};
        }
    }
    RegionPlan plan;
    plan.source_ = std::move(source);
    plan.destination_ = std::move(destination);
    for (std::size_t i = 0; i < plan.source_.format.planes.size(); ++i) {
        auto const &f = plan.source_.format.planes[i];
        if (rectangle.x % f.blockWidth || rectangle.width % f.blockWidth ||
            position.x % f.blockWidth || rectangle.y % f.blockHeight ||
            rectangle.height % f.blockHeight || position.y % f.blockHeight)
            return Error{ErrorCode::InvalidArgument,
                         "region is not aligned to the format sampling blocks"};
        PlaneRegion p{std::size_t{rectangle.x / f.blockWidth} * f.blockBytes,
                      rectangle.y / f.blockHeight,
                      std::size_t{position.x / f.blockWidth} * f.blockBytes,
                      position.y / f.blockHeight,
                      std::size_t{rectangle.width / f.blockWidth} * f.blockBytes,
                      rectangle.height / f.blockHeight,
                      dg.value()[i].rowBytes,
                      dg.value()[i].rows,
                      {}};
        if (background) {
            p.background.resize(f.blockBytes);
            for (auto const &sample : f.samples) {
                auto value =
                    std::find_if(background->begin(), background->end(), [&](auto const &v) {
                        return v.component == sample.component;
                    })->value;
                for (std::uint32_t bit = 0; bit < sample.bitCount; ++bit) {
                    auto address = std::size_t{sample.storageBit} + bit;
                    if ((value >> (sample.valueBit + bit)) & 1u)
                        p.background[address / 8] |= static_cast<std::byte>(1u << (address % 8));
                }
            }
        }
        plan.planes_.push_back(std::move(p));
    }
    return plan;
}
Result<RegionPlan> RegionPlan::crop(FrameDesc source, Rectangle rectangle) {
    auto destination = source;
    destination.width = rectangle.width;
    destination.height = rectangle.height;
    return prepare(std::move(source), std::move(destination), rectangle, {}, {});
}
Result<RegionPlan> RegionPlan::pad(FrameDesc source, FrameDesc destination, Point position,
                                   Background const &background) {
    Rectangle whole{0, 0, source.width, source.height};
    return prepare(std::move(source), std::move(destination), whole, position, background);
}
Result<void> RegionPlan::run(ConstFrameView const &source, FrameView const &destination) const {
    if (!(source.desc == source_) || !(destination.desc == destination_))
        return Error{ErrorCode::InvalidArgument, "image description does not match region plan"};
    auto status = validate(source);
    if (!status)
        return status;
    status = validate(destination);
    if (!status)
        return status;
    for (auto const &s : source.planes)
        for (auto const &d : destination.planes)
            if (overlaps(s, d))
                return Error{ErrorCode::InvalidArgument,
                             "region input and output allocations overlap"};
    for (std::size_t i = 0; i < planes_.size(); ++i) {
        auto const &p = planes_[i];
        for (std::size_t y = 0; y < p.rows; ++y) {
            auto *out = row(destination.planes[i], y);
            bool foreground = y >= p.destinationY && y - p.destinationY < p.copyRows;
            if (!p.background.empty()) {
                // Skip the foreground span: every output byte is written once.
                auto fill = [&](std::size_t begin, std::size_t end) {
                    for (auto x = begin; x < end; ++x)
                        out[x] = p.background[x % p.background.size()];
                };
                fill(0, foreground ? p.destinationX : p.rowBytes);
                if (foreground)
                    fill(p.destinationX + p.copyBytes, p.rowBytes);
            }
            if (foreground)
                std::memcpy(out + p.destinationX,
                            row(source.planes[i], p.sourceY + y - p.destinationY) + p.sourceX,
                            p.copyBytes);
        }
    }
    return {};
}
Result<void> crop(ConstFrameView const &source, Rectangle rectangle, FrameView const &destination) {
    auto plan = RegionPlan::crop(source.desc, rectangle);
    return plan ? plan.value().run(source, destination) : Result<void>{plan.error()};
}
Result<void> pad(ConstFrameView const &source, Point position, Background const &background,
                 FrameView const &destination) {
    auto plan = RegionPlan::pad(source.desc, destination.desc, position, background);
    return plan ? plan.value().run(source, destination) : Result<void>{plan.error()};
}
} // namespace lutils::image
