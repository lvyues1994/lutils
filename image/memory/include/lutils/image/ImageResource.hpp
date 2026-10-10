#pragma once
#include <lutils/compute/Memory.hpp>
#include <lutils/image/Region.hpp>

namespace lutils::image {
struct PlaneLayout {
    std::size_t object = 0;
    std::size_t offset = 0;
    std::ptrdiff_t stride = 0;
};
// Immutable linear layout over independently owned/imported storage objects.
struct ImageResource {
    static Result<ImageResource> linear(FrameDesc description,
                                        std::vector<compute::MemoryHandle> objects,
                                        std::vector<PlaneLayout> planes);
    static Result<ImageResource> borrow(ConstFrameView const &view);
    static Result<ImageResource> borrow(FrameView const &view);
    FrameDesc const &description() const noexcept { return description_; }
    std::vector<compute::MemoryHandle> const &objects() const noexcept { return objects_; }
    std::vector<PlaneLayout> const &planes() const noexcept { return planes_; }
    bool aliases(ImageResource const &other) const noexcept;

  private:
    ImageResource() = default;
    FrameDesc description_;
    std::vector<compute::MemoryHandle> objects_;
    std::vector<PlaneLayout> planes_;
};
struct RegionExecutor {
    virtual ~RegionExecutor() = default;
    // Caller serializes submissions and resource access. Retain each external
    // frame's access rights until completion; a reference only keeps storage alive.
    virtual Result<std::shared_ptr<compute::Completion>>
    submit(RegionPlan const &plan, ImageResource const &source,
           ImageResource const &destination) = 0;
    Result<void> run(RegionPlan const &plan, ImageResource const &source,
                     ImageResource const &destination);
};
std::unique_ptr<RegionExecutor> createCpuRegionExecutor();
// Performs imports once, so the result can be reused across submissions.
Result<ImageResource> bindImage(compute::Device &device, ImageResource const &image);
} // namespace lutils::image
