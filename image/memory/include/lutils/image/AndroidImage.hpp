#pragma once
#include <lutils/image/ImageResource.hpp>

struct AHardwareBuffer;
struct AImage;
namespace lutils::image {
struct AImageDeleter {
    void operator()(AImage *image) const noexcept;
};
using AImageOwner = std::unique_ptr<AImage, AImageDeleter>;
struct AndroidImageOptions {
    ColorSpec color;
    // AHB: optional producer lease. AImage: required owner of its AImageReader.
    std::shared_ptr<void> owner;
    // Borrowed sync-file; -1 means the producer is already ready.
    int acquireFence = -1;
};
Result<ImageResource> importAndroidImage(AHardwareBuffer *buffer, AndroidImageOptions options = {});
// Consumes image, including on failure. Do not call AImage plane APIs before
// adoption. The returned resource is read-only; mappings and GPU submissions
// retain the frame lease.
Result<ImageResource> adoptAndroidImage(AImageOwner image, AndroidImageOptions options);
} // namespace lutils::image
