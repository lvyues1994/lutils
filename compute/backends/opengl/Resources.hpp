#pragma once
#include "Context.hpp"
#include <lutils/compute/ExternalMemory.hpp>
#include <lutils/compute/NativeImage.hpp>
#include <map>

namespace lutils::compute::gl_detail {
struct Format {
    GLenum internal, layout, type;
};
Format format(kernel::ImageFormat value);
kernel::ImageFormat unsignedFormat(kernel::ImageFormat value);
bool esFormat(kernel::ImageFormat value);
GLenum textureTarget(ImageDesc const &image);
std::size_t imageBytes(ImageDesc const &image);
std::string shaderVersion(Context const &context);

struct Storage final : Buffer {
    explicit Storage(std::shared_ptr<Context> context) : context(std::move(context)) {}
    ~Storage() override;
    std::size_t wordCount() const noexcept override { return (bytes + 3) / 4; }
    std::size_t byteCount() const noexcept override { return bytes; }
    std::optional<ImageDesc> imageDescription() const override { return image; }
    bool writable() const noexcept override;
    bool aliases(Buffer const &other) const noexcept override;
    Result<FileDescriptor> acquire(Access access) const;
    Result<void> release(int fd, Access access) const;
    bool external() const { return memory || native; }
    std::shared_ptr<Context> context;
    GLuint object = 0;
    GLenum target = GL_SHADER_STORAGE_BUFFER;
    std::size_t bytes = 0;
    std::optional<ImageDesc> image;
    std::shared_ptr<ExternalMemory> memory;
    std::shared_ptr<NativeImage> native;
    EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
    // 0: ordinary 2D/3D texture; 1: raw external YUV; 2: external RGBA.
    unsigned samplerKind = 0;
    bool storageImage = true;
    bool gpuWritable = true;
};
struct Program final : Kernel {
    Program(std::shared_ptr<Context> context, KernelSource source, std::string text)
        : context(std::move(context)), description(std::move(source)), text(std::move(text)) {}
    ~Program() override;
    KernelSource const &source() const noexcept override { return description; }
    Result<GLuint> variant(std::vector<BufferHandle> const &buffers);
    std::shared_ptr<Context> context;
    KernelSource description;
    std::string text;
    std::map<std::vector<unsigned>, GLuint> programs;
};
Result<std::shared_ptr<Storage>> allocateBuffer(std::shared_ptr<Context> const &,
                                                std::size_t bytes);
Result<std::shared_ptr<Storage>> allocateImage(std::shared_ptr<Context> const &, ImageDesc const &);
Result<std::shared_ptr<Storage>> importBuffer(std::shared_ptr<Context> const &,
                                              MemoryHandle const &);
Result<std::shared_ptr<Storage>> importImage(std::shared_ptr<Context> const &,
                                             std::shared_ptr<NativeImage> const &);
Result<void> upload(Storage &, void const *data, std::size_t bytes);
} // namespace lutils::compute::gl_detail
