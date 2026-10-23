#pragma once
#include <lutils/compute/FileDescriptor.hpp>
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
enum class NativeImageFormat { Rgba8, Yuv420 };
struct NativeImageDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    NativeImageFormat format = NativeImageFormat::Rgba8;
};
// One logical plane. YUV uses Y, Cb, Cr; chroma addresses may be interleaved.
// capacity is the accessible byte span from data, including gaps between
// samples.
struct NativePlane {
    std::byte const *data = nullptr;
    std::size_t rowStride = 0;
    std::size_t pixelStride = 0;
    std::size_t capacity = 0;
};
struct NativeImageMapping {
    // Retains storage and its producer lease. finish() releases CPU access and
    // invalidates plane pointers; destruction must also finish outstanding
    // access.
    virtual ~NativeImageMapping() = default;
    virtual std::vector<NativePlane> const &planes() const noexcept = 0;
    virtual Result<std::byte *> writablePlane(std::size_t plane) = 0;
    virtual Result<void> finish() = 0;
};
// Opaque image storage has no flat byte-addressable layout. Callers serialize
// access.
struct NativeImage {
    virtual ~NativeImage() = default;
    virtual NativeImageDesc description() const noexcept = 0;
    virtual bool writable() const noexcept = 0;
    virtual bool aliases(NativeImage const &other) const noexcept = 0;
    virtual bool aliasesMemory(Memory const &) const noexcept { return false; }
    virtual Result<std::unique_ptr<NativeImageMapping>> map(Access access) = 0;
    virtual Result<FileDescriptor> exportSyncFile(Access access) const = 0;
    virtual Result<void> importSyncFile(int fd, Access access) const = 0;
};
} // namespace lutils::compute
