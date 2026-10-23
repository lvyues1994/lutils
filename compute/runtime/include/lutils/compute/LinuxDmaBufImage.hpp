#pragma once
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/compute/NativeImage.hpp>

namespace lutils::compute {
struct DmaBufImagePlane {
    int fd = -1;
    std::uint32_t offset = 0, stride = 0;
    // Explicit DRM modifier; 0 means linear. An absent/implicit modifier is not
    // accepted.
    std::uint64_t modifier = 0;
};
struct DmaBufNativeImageDesc {
    NativeImageDesc image;
    // DRM_FORMAT_ABGR8888 (RGBA byte order), NV12, NV21 or YUV420.
    std::uint32_t fourcc = 0;
    std::vector<DmaBufImagePlane> planes;
    bool readOnly = false;
};
// Owns fd duplicates and preserves layout metadata for native GPU import.
// CPU mapping is available only for modifier 0; GPU import queries the driver.
struct DmaBufNativeImage : NativeImage {
    static Result<std::shared_ptr<DmaBufNativeImage>> import(DmaBufNativeImageDesc description);
    virtual DmaBufNativeImageDesc const &dmaDescription() const noexcept = 0;
};
} // namespace lutils::compute
