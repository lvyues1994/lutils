#pragma once

#include <lutils/image/Frame.hpp>
#include <optional>

namespace lutils::image {
struct Point {
    std::uint32_t x = 0, y = 0;
};
struct Rectangle {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};
struct ComponentValue {
    Component component;
    std::uint32_t value;
};
// Values are already encoded in the destination color space and range.
using Background = std::vector<ComponentValue>;
struct PlaneRegion {
    std::size_t sourceX, sourceY, destinationX, destinationY;
    std::size_t copyBytes, copyRows, rowBytes, rows;
    std::vector<std::byte> background;
};

// A validated, reusable operation; image storage is supplied at execution time.
struct RegionPlan {
    static Result<RegionPlan> crop(FrameDesc source, Rectangle rectangle);
    static Result<RegionPlan> pad(FrameDesc source, FrameDesc destination, Point position,
                                  Background const &background);
    FrameDesc const &source() const noexcept { return source_; }
    FrameDesc const &destination() const noexcept { return destination_; }
    std::vector<PlaneRegion> const &planes() const noexcept { return planes_; }
    Result<void> run(ConstFrameView const &source, FrameView const &destination) const;

  private:
    RegionPlan() = default;
    static Result<RegionPlan> prepare(FrameDesc source, FrameDesc destination, Rectangle rectangle,
                                      Point position, std::optional<Background> background);
    FrameDesc source_, destination_;
    std::vector<PlaneRegion> planes_;
};
Result<void> crop(ConstFrameView const &source, Rectangle rectangle, FrameView const &destination);
Result<void> pad(ConstFrameView const &source, Point position, Background const &background,
                 FrameView const &destination);
} // namespace lutils::image
