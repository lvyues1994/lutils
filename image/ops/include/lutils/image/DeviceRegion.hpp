#pragma once
#include <lutils/image/ImageResource.hpp>

namespace lutils::image {
// Device outlives the executor. Import images with bindImage() to reuse bindings.
// This executor records only dispatches: no upload, readback or CPU fallback.
Result<std::unique_ptr<RegionExecutor>> createDeviceRegionExecutor(compute::Device &device);
} // namespace lutils::image
