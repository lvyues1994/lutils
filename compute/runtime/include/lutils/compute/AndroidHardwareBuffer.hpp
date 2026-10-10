#pragma once
#include <lutils/compute/ExternalMemory.hpp>

struct AHardwareBuffer;
namespace lutils::compute {
// Linear BLOB storage only. Native RGBA/YUV AHB images have a different layout.
// import() acquires a reference; the caller retains its original reference.
struct AndroidHardwareBufferMemory : ExternalMemory {
    static Result<std::shared_ptr<AndroidHardwareBufferMemory>> import(AHardwareBuffer *buffer);
    static Result<std::shared_ptr<AndroidHardwareBufferMemory>> allocate(std::size_t bytes);
    static bool available() noexcept;
    virtual AHardwareBuffer *nativeBuffer() const noexcept = 0;
};
} // namespace lutils::compute
