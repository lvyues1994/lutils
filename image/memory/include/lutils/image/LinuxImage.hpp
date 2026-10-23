#pragma once
#include <lutils/compute/LinuxDmaBufImage.hpp>
#include <lutils/image/ImageResource.hpp>

namespace lutils::image {
struct DmaBufImageDesc {
    FrameDesc image;
    std::vector<int> fds;
    std::vector<PlaneLayout> planes;
    // Explicit DRM_FORMAT_MOD_LINEAR (0) is required. Missing is not linear.
    std::optional<std::uint64_t> modifier;
};
Result<ImageResource> importDmaBufImage(DmaBufImageDesc const &description);
// Preserves DRM metadata for EGL/native-image import. Linear CPU access remains
// available.
Result<ImageResource> importNativeDmaBufImage(compute::DmaBufNativeImageDesc description,
                                              ColorSpec color = {}, Rectangle visible = {});
} // namespace lutils::image
