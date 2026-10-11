#pragma once
#include <lutils/compute/NativeImage.hpp>

struct AHardwareBuffer;
namespace lutils::compute {
struct AndroidHardwareBufferImage : NativeImage {
    // Acquires a native reference. The optional owner retains producer access
    // rights.
    static Result<std::shared_ptr<AndroidHardwareBufferImage>>
    import(AHardwareBuffer *buffer, std::shared_ptr<void> owner = {}, bool readOnly = false);
    virtual AHardwareBuffer *nativeBuffer() const noexcept = 0;
};
} // namespace lutils::compute
