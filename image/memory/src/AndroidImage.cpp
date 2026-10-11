#include <lutils/compute/AndroidHardwareBufferImage.hpp>
#include <lutils/image/AndroidImage.hpp>
#include <media/NdkImage.h>

namespace lutils::image {
namespace {
Result<ImageResource> import(AHardwareBuffer *buffer, AndroidImageOptions options, bool readOnly,
                             Rectangle visible = {}) {
    auto native =
        compute::AndroidHardwareBufferImage::import(buffer, std::move(options.owner), readOnly);
    if (!native)
        return native.error();
    auto fence = native.value()->importSyncFile(options.acquireFence, compute::Access::Read);
    if (!fence)
        return fence.error();
    return ImageResource::native(std::move(native).value(), options.color, visible);
}
struct ImageLease {
    std::shared_ptr<void> reader;
    AImageOwner image;
    ImageLease(std::shared_ptr<void> r, AImageOwner i)
        : reader(std::move(r)), image(std::move(i)) {}
};
} // namespace
void AImageDeleter::operator()(AImage *image) const noexcept {
    if (image)
        AImage_delete(image);
}
Result<ImageResource> importAndroidImage(AHardwareBuffer *buffer, AndroidImageOptions options) {
    return import(buffer, std::move(options), false);
}
Result<ImageResource> adoptAndroidImage(AImageOwner image, AndroidImageOptions options) {
    // Keep the reader alive even if a later metadata check fails.
    auto lease = std::make_shared<ImageLease>(std::move(options.owner), std::move(image));
    if (!lease->reader || !lease->image)
        return Error{ErrorCode::InvalidArgument,
                     "AImage adoption requires an image and its reader owner"};
    AHardwareBuffer *buffer = nullptr;
    AImageCropRect crop{};
    std::int32_t width = 0, height = 0, format = 0;
    if (AImage_getHardwareBuffer(lease->image.get(), &buffer) != AMEDIA_OK ||
        AImage_getCropRect(lease->image.get(), &crop) != AMEDIA_OK ||
        AImage_getWidth(lease->image.get(), &width) != AMEDIA_OK ||
        AImage_getHeight(lease->image.get(), &height) != AMEDIA_OK ||
        AImage_getFormat(lease->image.get(), &format) != AMEDIA_OK)
        return Error{ErrorCode::Device, "AImage metadata query failed"};
    if (format != AIMAGE_FORMAT_RGBA_8888 && format != AIMAGE_FORMAT_YUV_420_888)
        return Error{ErrorCode::Unsupported, "AImage format must be RGBA_8888 or YUV_420_888"};
    if (crop.left < 0 || crop.top < 0 || crop.right <= crop.left || crop.bottom <= crop.top ||
        crop.right > width || crop.bottom > height)
        return Error{ErrorCode::Bounds, "invalid AImage crop rectangle"};
    options.owner = std::move(lease);
    return import(buffer, std::move(options), true,
                  {static_cast<std::uint32_t>(crop.left), static_cast<std::uint32_t>(crop.top),
                   static_cast<std::uint32_t>(crop.right - crop.left),
                   static_cast<std::uint32_t>(crop.bottom - crop.top)});
}
} // namespace lutils::image
