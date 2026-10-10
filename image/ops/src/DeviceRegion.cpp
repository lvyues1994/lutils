#include <algorithm>
#include <image_region_bytes.hpp>
#include <lutils/image/DeviceRegion.hpp>

namespace lutils::image {
namespace {
namespace co = compute;
struct DeviceRegions final : RegionExecutor {
    co::Device &device;
    co::KernelHandle kernel;
    DeviceRegions(co::Device &d, co::KernelHandle k) : device(d), kernel(std::move(k)) {}
    Result<std::shared_ptr<co::Completion>> submit(RegionPlan const &plan,
                                                   ImageResource const &source,
                                                   ImageResource const &destination) override {
        if (!(source.description() == plan.source()) ||
            !(destination.description() == plan.destination()) || source.aliases(destination))
            return Error{ErrorCode::InvalidArgument, "region resources mismatch or alias"};
        for (auto const &m : destination.objects())
            if (!m->writable())
                return Error{ErrorCode::InvalidArgument, "region output is read-only"};
        // Validate all layouts before importing resources or submitting any work.
        for (auto const *image : {&source, &destination}) {
            for (auto const &m : image->objects())
                if (m->byteCount() > UINT32_MAX || m->byteCount() % 4)
                    return Error{
                        ErrorCode::Unsupported,
                        "device regions need whole-word capacity within 32-bit byte indexing"};
            for (auto const &p : image->planes())
                if (p.stride <= 0 || static_cast<std::size_t>(p.stride) > UINT32_MAX)
                    return Error{ErrorCode::Unsupported,
                                 "device regions require positive 32-bit strides"};
        }
        for (auto const &p : plan.planes())
            if (p.background.size() > 4)
                return Error{ErrorCode::Unsupported, "device background block exceeds four bytes"};
        auto input = bindImage(device, source);
        if (!input)
            return input.error();
        auto output = bindImage(device, destination);
        if (!output)
            return output.error();
        if (input.value().aliases(output.value()))
            return Error{ErrorCode::InvalidArgument, "device region resources alias"};
        co::CommandList commands;
        for (std::size_t i = 0; i < plan.planes().size(); ++i) {
            auto const &p = plan.planes()[i];
            auto const &s = input.value().planes()[i];
            auto const &d = output.value().planes()[i];
            auto sourceBuffer = input.value().objects()[s.object]->residentBuffer();
            auto destinationBuffer = output.value().objects()[d.object]->residentBuffer();
            auto first = d.offset / 4;
            auto end = d.offset + (p.rows - 1) * static_cast<std::size_t>(d.stride) + p.rowBytes;
            auto count = (end + 3) / 4 - first;
            auto width = std::min<std::size_t>(count, 16384);
            co::Extent3 extent{static_cast<co::Word>(width),
                               static_cast<co::Word>((count + width - 1) / width), 1};
            co::Word background = 0;
            for (std::size_t b = 0; b < p.background.size(); ++b)
                background |= std::to_integer<co::Word>(p.background[b]) << (b * 8);
            std::vector<co::Word> params{extent.x, extent.y, 1, 0};
            for (auto value :
                 {first, count, width, s.offset, static_cast<std::size_t>(s.stride), p.sourceX,
                  p.sourceY, d.offset, std::max(static_cast<std::size_t>(d.stride), p.rowBytes),
                  p.destinationX, p.destinationY, p.copyBytes, p.copyRows, p.rowBytes, p.rows,
                  static_cast<std::size_t>(background),
                  std::max<std::size_t>(1, p.background.size())})
                params.push_back(static_cast<co::Word>(value));
            auto recorded = commands.dispatch(
                {kernel, {sourceBuffer, destinationBuffer}, std::move(params), extent});
            if (!recorded)
                return recorded.error();
        }
        return device.submit(commands);
    }
};
} // namespace
Result<std::unique_ptr<RegionExecutor>> createDeviceRegionExecutor(compute::Device &device) {
    auto kernel = device.createKernel(compute::KernelTraits<kernels::RegionBytes>::source());
    if (!kernel)
        return kernel.error();
    return std::unique_ptr<RegionExecutor>{
        std::make_unique<DeviceRegions>(device, std::move(kernel).value())};
}
} // namespace lutils::image
