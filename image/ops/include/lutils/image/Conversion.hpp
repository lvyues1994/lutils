#pragma once
#include <lutils/compute/Runtime.hpp>
#include <lutils/image/Frame.hpp>

namespace lutils::image {
struct DevicePlane {
    std::uint32_t offsetWords;
    std::uint32_t strideWords;
    std::uint32_t rows;
    std::uint32_t rowBytes;
};
struct DeviceLayout {
    std::vector<DevicePlane> planes;
    std::size_t words;
};
Result<DeviceLayout> deviceLayout(FrameDesc const &desc);
struct DeviceFrame {
    static Result<DeviceFrame> create(compute::Device &device, FrameDesc desc);
    FrameDesc const &description() const noexcept { return desc_; }
    DeviceLayout const &layout() const noexcept { return layout_; }
    compute::BufferHandle const &buffer() const noexcept { return buffer_; }

  private:
    DeviceFrame() = default;
    FrameDesc desc_;
    DeviceLayout layout_;
    compute::BufferHandle buffer_;
};
struct FrameReadback {
    // Waits for this result, then writes to the view supplied now; no host view
    // is retained.
    Result<void> copyTo(compute::Completion &completion, FrameView const &destination) const;

  private:
    FrameReadback() = default;
    FrameDesc desc_;
    DeviceLayout layout_;
    compute::ReadbackToken token_;
    friend Result<FrameReadback> recordReadback(compute::CommandList &, DeviceFrame const &);
};
Result<void> recordUpload(compute::CommandList &commands, ConstFrameView const &source,
                          DeviceFrame const &destination);
Result<FrameReadback> recordReadback(compute::CommandList &commands, DeviceFrame const &source);
Result<void> upload(compute::Device &device, ConstFrameView const &source,
                    DeviceFrame const &destination);
Result<void> download(compute::Device &device, DeviceFrame const &source,
                      FrameView const &destination);

struct ConversionPlan {
    static Result<ConversionPlan> prepare(compute::Device &device, FrameDesc source,
                                          FrameDesc destination);
    Result<void> record(compute::CommandList &commands, DeviceFrame const &source,
                        DeviceFrame const &destination) const;
    Result<void> run(compute::Device &device, ConstFrameView const &source,
                     FrameView const &destination) const;

  private:
    ConversionPlan() = default;
    FrameDesc source_;
    FrameDesc destination_;
    compute::KernelHandle kernel_;
    std::vector<compute::Word> parameters_;
    compute::Extent3 extent_;
};
} // namespace lutils::image
