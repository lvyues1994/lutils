#include "Resources.hpp"
#include <algorithm>
#include <limits>
#include <lutils/compute/LinuxDmaBufImage.hpp>
#include <sstream>
#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <lutils/compute/AndroidHardwareBufferImage.hpp>
#endif

namespace lutils::compute::gl_detail {
Format format(kernel::ImageFormat value) {
    using F = kernel::ImageFormat;
    // Sized internal formats are shared by GL and ES; the 16-bit normalized
    // formats are desktop-only and are deliberately not exposed on ES.
    switch (value) {
#define FMT(N, I, L, T)                                                                            \
    case F::N:                                                                                     \
        return {I, L, T};
        FMT(R8, GL_R8, GL_RED, GL_UNSIGNED_BYTE)
        FMT(RG8, GL_RG8, GL_RG, GL_UNSIGNED_BYTE)
        FMT(RGBA8, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE)
        FMT(R8Snorm, GL_R8_SNORM, GL_RED, GL_BYTE)
        FMT(RG8Snorm, GL_RG8_SNORM, GL_RG, GL_BYTE)
        FMT(RGBA8Snorm, GL_RGBA8_SNORM, GL_RGBA, GL_BYTE)
        FMT(R8UI, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE)
        FMT(RG8UI, GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE)
        FMT(RGBA8UI, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE)
        FMT(R8I, GL_R8I, GL_RED_INTEGER, GL_BYTE)
        FMT(RG8I, GL_RG8I, GL_RG_INTEGER, GL_BYTE)
        FMT(RGBA8I, GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE)
        FMT(R16, 0x822A, GL_RED, GL_UNSIGNED_SHORT)
        FMT(RG16, 0x822C, GL_RG, GL_UNSIGNED_SHORT)
        FMT(RGBA16, 0x805B, GL_RGBA, GL_UNSIGNED_SHORT)
        FMT(R16Snorm, 0x8F98, GL_RED, GL_SHORT)
        FMT(RG16Snorm, 0x8F99, GL_RG, GL_SHORT)
        FMT(RGBA16Snorm, 0x8F9B, GL_RGBA, GL_SHORT)
        FMT(R16UI, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT)
        FMT(RG16UI, GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT)
        FMT(RGBA16UI, GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT)
        FMT(R16I, GL_R16I, GL_RED_INTEGER, GL_SHORT)
        FMT(RG16I, GL_RG16I, GL_RG_INTEGER, GL_SHORT)
        FMT(RGBA16I, GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT)
        FMT(R32UI, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT)
        FMT(RG32UI, GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT)
        FMT(RGBA32UI, GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT)
        FMT(R32I, GL_R32I, GL_RED_INTEGER, GL_INT)
        FMT(RG32I, GL_RG32I, GL_RG_INTEGER, GL_INT)
        FMT(RGBA32I, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT)
        FMT(R32F, GL_R32F, GL_RED, GL_FLOAT)
        FMT(RG32F, GL_RG32F, GL_RG, GL_FLOAT)
        FMT(RGBA32F, GL_RGBA32F, GL_RGBA, GL_FLOAT)
        FMT(R16F, GL_R16F, GL_RED, GL_HALF_FLOAT)
        FMT(RG16F, GL_RG16F, GL_RG, GL_HALF_FLOAT)
        FMT(RGBA16F, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT)
#undef FMT
    }
    return {};
}
bool esFormat(kernel::ImageFormat value) {
    switch (value) {
    case kernel::ImageFormat::RGBA32F:
    case kernel::ImageFormat::RGBA16F:
    case kernel::ImageFormat::R32F:
    case kernel::ImageFormat::RGBA32UI:
    case kernel::ImageFormat::RGBA16UI:
    case kernel::ImageFormat::RGBA8UI:
    case kernel::ImageFormat::R32UI:
    case kernel::ImageFormat::RGBA32I:
    case kernel::ImageFormat::RGBA16I:
    case kernel::ImageFormat::RGBA8I:
    case kernel::ImageFormat::R32I:
    case kernel::ImageFormat::RGBA8:
    case kernel::ImageFormat::RGBA8Snorm:
        return true;
    default:
        return false;
    }
}
kernel::ImageFormat unsignedFormat(kernel::ImageFormat value) {
    using F = kernel::ImageFormat;
    auto info = kernel::formatInfo(value);
    auto bytes = info.bytes / info.channels;
    if (info.channels == 1)
        return bytes == 1 ? F::R8UI : bytes == 2 ? F::R16UI : F::R32UI;
    if (info.channels == 2)
        return bytes == 1 ? F::RG8UI : bytes == 2 ? F::RG16UI : F::RG32UI;
    return bytes == 1 ? F::RGBA8UI : bytes == 2 ? F::RGBA16UI : F::RGBA32UI;
}
GLenum textureTarget(ImageDesc const &image) {
    return image.dimensions == 1   ? GLenum{0x0DE0}
           : image.dimensions == 2 ? GL_TEXTURE_2D
                                   : GL_TEXTURE_3D;
}
std::size_t imageBytes(ImageDesc const &image) {
    return kernel::formatInfo(image.format).bytes * image.extent.x * image.extent.y *
           image.extent.z;
}
std::string shaderVersion(Context const &context) {
    return context.options.target == GlslTarget::OpenGl43
               ? "#version 430 core\n"
               : "#version 310 es\nprecision highp float;\nprecision highp "
                 "int;\n";
}
Storage::~Storage() {
    context->invoke([&] {
        if (object) {
            if (image)
                context->gl.DeleteTextures(1, &object);
            else
                context->gl.DeleteBuffers(1, &object);
        }
        if (eglImage != EGL_NO_IMAGE_KHR)
            context->destroyImage(context->display, eglImage);
    });
}
bool Storage::writable() const noexcept {
    return gpuWritable && (native ? native->writable() : memory ? memory->writable() : true);
}
bool Storage::aliases(Buffer const &other) const noexcept {
    if (this == &other)
        return true;
    auto p = dynamic_cast<Storage const *>(&other);
    return p && ((native && p->native && native->aliases(*p->native)) ||
                 (memory && p->memory && memory->aliases(*p->memory)));
}
Result<FileDescriptor> Storage::acquire(Access access) const {
    return native ? native->exportSyncFile(access) : memory->exportSyncFile(access);
}
Result<void> Storage::release(int fd, Access access) const {
    return native ? native->importSyncFile(fd, access) : memory->importSyncFile(fd, access);
}
Result<std::shared_ptr<Storage>> allocateBuffer(std::shared_ptr<Context> const &context,
                                                std::size_t bytes) {
    if (bytes > context->info.maxBufferBytes - context->info.maxBufferBytes % 4 ||
        bytes > std::size_t{UINT32_MAX} * 4 ||
        bytes > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()))
        return Error{ErrorCode::Overflow, "buffer exceeds GL storage/indexing limits"};
    auto result = std::make_shared<Storage>(context);
    result->bytes = bytes;
    auto physicalBytes = result->wordCount() * 4;
    auto &gl = context->gl;
    gl.GenBuffers(1, &result->object);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, result->object);
    // BufferData with null data is uninitialized. A single reusable chunk keeps
    // initial contents deterministic without a second allocation the buffer's
    // size.
    gl.BufferData(GL_SHADER_STORAGE_BUFFER,
                  static_cast<GLsizeiptr>(std::max<std::size_t>(physicalBytes, 4)), nullptr,
                  GL_DYNAMIC_COPY);
    std::array<std::byte, 65536> zero{};
    for (std::size_t offset = 0; offset < physicalBytes; offset += zero.size()) {
        auto count = std::min(zero.size(), physicalBytes - offset);
        gl.BufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(offset),
                         static_cast<GLsizeiptr>(count), zero.data());
    }
    auto checked = context->error("allocate buffer");
    if (!checked)
        return checked.error();
    return result;
}
Result<void> upload(Storage &storage, void const *data, std::size_t bytes) {
    auto &context = *storage.context;
    auto &gl = context.gl;
    if (!storage.image) {
        gl.BindBuffer(GL_COPY_WRITE_BUFFER, storage.object);
        if (bytes)
            gl.BufferSubData(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
    } else {
        auto const &desc = *storage.image;
        auto f = format(desc.format);
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.BindTexture(storage.target, storage.object);
        auto x = static_cast<GLsizei>(desc.extent.x), y = static_cast<GLsizei>(desc.extent.y),
             z = static_cast<GLsizei>(desc.extent.z);
        if (desc.dimensions == 1)
            gl.TexSubImage1D(storage.target, 0, 0, x, f.layout, f.type, data);
        else if (desc.dimensions == 2)
            gl.TexSubImage2D(storage.target, 0, 0, 0, x, y, f.layout, f.type, data);
        else
            gl.TexSubImage3D(storage.target, 0, 0, 0, 0, x, y, z, f.layout, f.type, data);
    }
    return context.error("upload");
}
Result<std::shared_ptr<Storage>> allocateImage(std::shared_ptr<Context> const &context,
                                               ImageDesc const &desc) {
    auto count = imageWordCount(desc);
    if (!count)
        return count.error();
    if (context->options.target == GlslTarget::OpenGles31 &&
        (desc.dimensions == 1 || !esFormat(desc.format)))
        return Error{ErrorCode::Unsupported,
                     "image format/dimension is unavailable in the GLES 3.1 "
                     "storage image profile"};
    auto limit =
        static_cast<Word>(desc.dimensions == 3 ? context->maxTexture3d : context->maxTexture);
    if (desc.extent.x > limit || desc.extent.y > limit || desc.extent.z > limit)
        return Error{ErrorCode::Unsupported, "image exceeds GL texture dimensions"};
    auto result = std::make_shared<Storage>(context);
    result->image = desc;
    result->bytes = imageBytes(desc);
    result->target = textureTarget(desc);
    auto &gl = context->gl;
    auto internal = format(desc.format).internal;
    gl.GenTextures(1, &result->object);
    gl.BindTexture(result->target, result->object);
    gl.TexParameteri(result->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(result->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(result->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(result->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    auto x = static_cast<GLsizei>(desc.extent.x), y = static_cast<GLsizei>(desc.extent.y),
         z = static_cast<GLsizei>(desc.extent.z);
    if (desc.dimensions == 1)
        gl.TexStorage1D(result->target, 1, internal, x);
    else if (desc.dimensions == 2)
        gl.TexStorage2D(result->target, 1, internal, x, y);
    else
        gl.TexStorage3D(result->target, 1, internal, x, y, z);
    auto checked = context->error("allocate texture");
    if (!checked)
        return checked.error();
    std::vector<Word> zero(count.value());
    checked = upload(*result, zero.data(), result->bytes);
    if (!checked)
        return checked.error();
    return result;
}
Program::~Program() {
    context->invoke([&] {
        for (auto const &p : programs)
            context->gl.DeleteProgram(p.second);
    });
}
Result<GLuint> Program::variant(std::vector<BufferHandle> const &buffers) {
    std::vector<unsigned> kinds(description.bindings.size());
    for (std::size_t i = 0; i < buffers.size(); ++i)
        kinds[i] = static_cast<Storage const &>(*buffers[i]).samplerKind;
    auto found = programs.find(kinds);
    if (found != programs.end())
        return found->second;
    std::string additions;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (!kinds[i])
            continue;
        if (description.resources.empty() || !description.resources[i].sampled)
            return Error{ErrorCode::Unsupported, "external image requires a sampled binding"};
        if (!context->extension("GL_OES_EGL_image_external_essl3") ||
            (kinds[i] == 1 && !context->extension("GL_EXT_YUV_target")))
            return Error{ErrorCode::Unsupported,
                         "raw external image sampling extension is unavailable"};
        additions += "#extension GL_OES_EGL_image_external_essl3 : require\n";
        if (kinds[i] == 1)
            additions += "#extension GL_EXT_YUV_target : require\n";
        auto name = "b" + std::to_string(i);
        additions += "#define LUTILS_SAMPLER_" + name +
                     (kinds[i] == 1 ? " __samplerExternal2DY2YEXT\n" : " samplerExternalOES\n");
        additions += "#define LUTILS_TEXTURE_" + name + "(uv) texture(" + name + ",uv)" +
                     (kinds[i] == 1 ? ".brga\n" : "\n");
    }
    auto source = text;
    if (!additions.empty()) {
        auto marker = source.find("// LUTILS_EXTERNAL_EXTENSIONS");
        if (marker == std::string::npos)
            return Error{ErrorCode::Unsupported, "shader does not support external samplers"};
        source.insert(marker, additions);
    }
    auto compiled = context->program(source);
    if (!compiled)
        return compiled.error();
    GLint local[3]{};
    context->gl.GetProgramiv(compiled.value(), GL_COMPUTE_WORK_GROUP_SIZE, local);
    if (static_cast<Word>(local[0]) != description.localSize.x ||
        static_cast<Word>(local[1]) != description.localSize.y ||
        static_cast<Word>(local[2]) != description.localSize.z) {
        context->gl.DeleteProgram(compiled.value());
        return Error{ErrorCode::InvalidArgument, "GLSL local size does not match kernel metadata"};
    }
    programs.emplace(std::move(kinds), compiled.value());
    return compiled.value();
}
Result<std::shared_ptr<Storage>> importBuffer(std::shared_ptr<Context> const &context,
                                              MemoryHandle const &memory) {
#ifdef __ANDROID__
    auto ahb = std::dynamic_pointer_cast<AndroidHardwareBufferMemory>(memory);
    if (ahb && context->gl.BufferStorageExternal && context->nativeClientBuffer &&
        context->extension("GL_EXT_external_buffer") &&
        context->extension("GL_EXT_buffer_storage")) {
        if (!memory->byteCount() || memory->byteCount() % 4 ||
            memory->byteCount() > context->info.maxBufferBytes)
            return Error{ErrorCode::Unsupported, "external buffer size exceeds GL limits"};
        auto result = std::make_shared<Storage>(context);
        result->bytes = memory->byteCount();
        result->memory = ahb;
        auto &gl = context->gl;
        gl.GenBuffers(1, &result->object);
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, result->object);
        gl.BufferStorageExternal(
            GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(result->bytes),
            context->nativeClientBuffer(ahb->nativeBuffer()), GL_DYNAMIC_STORAGE_BIT_EXT);
        auto checked = context->error("import AHB buffer");
        if (!checked)
            return checked.error();
        return result;
    }
#else
    (void)context;
    (void)memory;
#endif
    return Error{ErrorCode::Unsupported, "GL cannot bind this external allocation as an SSBO"};
}
Result<std::shared_ptr<Storage>> importImage(std::shared_ptr<Context> const &context,
                                             std::shared_ptr<NativeImage> const &image) {
    if (!image)
        return Error{ErrorCode::InvalidArgument, "null native image"};
    auto nativeDesc = image->description();
    if (!nativeDesc.width || !nativeDesc.height ||
        nativeDesc.width > static_cast<Word>(context->maxTexture) ||
        nativeDesc.height > static_cast<Word>(context->maxTexture))
        return Error{ErrorCode::Unsupported, "native image exceeds GL texture dimensions"};
    if (auto dma = std::dynamic_pointer_cast<DmaBufNativeImage>(image)) {
        if (!context->info.capabilities.externalDmaBuf ||
            !context->eglExtension("EGL_EXT_image_dma_buf_import_modifiers"))
            return Error{ErrorCode::Unsupported,
                         "explicit dma-buf image modifiers are unavailable"};
        auto query = procedure<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>("eglQueryDmaBufModifiersEXT");
        if (!query)
            return Error{ErrorCode::Unsupported, "EGL dma-buf modifier query is unavailable"};
        auto const &desc = dma->dmaDescription();
        EGLint count = 0;
        if (!query(context->display, static_cast<EGLint>(desc.fourcc), 0, nullptr, nullptr,
                   &count) ||
            count <= 0)
            return Error{ErrorCode::Unsupported, "EGL does not support this DRM FourCC"};
        std::vector<EGLuint64KHR> modifiers(static_cast<std::size_t>(count));
        std::vector<EGLBoolean> externalOnly(static_cast<std::size_t>(count));
        if (!query(context->display, static_cast<EGLint>(desc.fourcc), count, modifiers.data(),
                   externalOnly.data(), &count))
            return Error{ErrorCode::Device, "EGL dma-buf modifier query failed"};
        bool external = nativeDesc.format == NativeImageFormat::Yuv420;
        for (auto const &plane : desc.planes) {
            auto found = std::find(modifiers.begin(), modifiers.begin() + count, plane.modifier);
            if (found == modifiers.begin() + count)
                return Error{ErrorCode::Unsupported, "EGL does not support the dma-buf modifier"};
            external =
                external ||
                externalOnly[static_cast<std::size_t>(found - modifiers.begin())] == EGL_TRUE;
        }
        bool yuv = nativeDesc.format == NativeImageFormat::Yuv420;
        if (external && (!context->extension("GL_OES_EGL_image_external_essl3") ||
                         (yuv && !context->extension("GL_EXT_YUV_target"))))
            return Error{ErrorCode::Unsupported,
                         "external/raw YUV dma-buf sampling is unavailable"};
        std::vector<EGLint> attributes{EGL_WIDTH,
                                       static_cast<EGLint>(nativeDesc.width),
                                       EGL_HEIGHT,
                                       static_cast<EGLint>(nativeDesc.height),
                                       EGL_LINUX_DRM_FOURCC_EXT,
                                       static_cast<EGLint>(desc.fourcc)};
        constexpr EGLint fdKeys[]{EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT,
                                  EGL_DMA_BUF_PLANE2_FD_EXT};
        constexpr EGLint offsetKeys[]{EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                                      EGL_DMA_BUF_PLANE2_OFFSET_EXT};
        constexpr EGLint pitchKeys[]{EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                     EGL_DMA_BUF_PLANE2_PITCH_EXT};
        constexpr EGLint lowKeys[]{EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                                   EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                                   EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT};
        constexpr EGLint highKeys[]{EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                                    EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                                    EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT};
        for (std::size_t i = 0; i < desc.planes.size(); ++i) {
            auto const &p = desc.planes[i];
            attributes.insert(attributes.end(),
                              {fdKeys[i], p.fd, offsetKeys[i], static_cast<EGLint>(p.offset),
                               pitchKeys[i], static_cast<EGLint>(p.stride), lowKeys[i],
                               static_cast<EGLint>(p.modifier & UINT32_MAX), highKeys[i],
                               static_cast<EGLint>(p.modifier >> 32)});
        }
        attributes.push_back(EGL_NONE);
        auto result = std::make_shared<Storage>(context);
        result->native = image;
        result->image =
            ImageDesc{kernel::ImageFormat::RGBA8, 2, {nativeDesc.width, nativeDesc.height, 1}};
        result->bytes = imageBytes(*result->image);
        result->target = external ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
        result->samplerKind = yuv ? 1u : external ? 2u : 0u;
        bool immutable = !external && context->gl.ImageTargetStorage &&
                         context->extension("GL_EXT_EGL_image_storage");
        result->storageImage =
            !external && (immutable || context->options.target == GlslTarget::OpenGl43);
        result->eglImage = context->createImage(context->display, EGL_NO_CONTEXT,
                                                EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
        if (result->eglImage == EGL_NO_IMAGE_KHR)
            return Error{ErrorCode::Unsupported, "EGL rejected dma-buf image import"};
        auto &gl = context->gl;
        gl.GenTextures(1, &result->object);
        gl.BindTexture(result->target, result->object);
        gl.TexParameteri(result->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(result->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(result->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(result->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (immutable)
            gl.ImageTargetStorage(result->target, result->eglImage, nullptr);
        else
            gl.ImageTargetTexture(result->target, result->eglImage);
        auto checked = context->error("bind dma-buf image");
        if (!checked)
            return checked.error();
        return result;
    }
#ifdef __ANDROID__
    auto ahb = std::dynamic_pointer_cast<AndroidHardwareBufferImage>(image);
    if (ahb && context->info.capabilities.externalAndroidHardwareBuffer) {
        AHardwareBuffer_Desc allocation{};
        AHardwareBuffer_describe(ahb->nativeBuffer(), &allocation);
        if (!(allocation.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE))
            return Error{ErrorCode::Unsupported, "AHB image was not allocated for GPU sampling"};
        auto desc = image->description();
        auto yuv = desc.format == NativeImageFormat::Yuv420;
        if (yuv && (!context->extension("GL_EXT_YUV_target") ||
                    !context->extension("GL_OES_EGL_image_external_essl3")))
            return Error{ErrorCode::Unsupported, "raw AHB YUV sampling is unavailable"};
        auto result = std::make_shared<Storage>(context);
        result->native = image;
        result->image = ImageDesc{kernel::ImageFormat::RGBA8, 2, {desc.width, desc.height, 1}};
        result->bytes = imageBytes(*result->image);
        result->target = yuv ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
        result->samplerKind = yuv ? 1u : 0u;
        result->gpuWritable = !yuv && (allocation.usage & AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT);
        result->storageImage = !yuv && context->gl.ImageTargetStorage &&
                               context->extension("GL_EXT_EGL_image_storage");
        EGLint attributes[]{EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        result->eglImage =
            context->createImage(context->display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                 context->nativeClientBuffer(ahb->nativeBuffer()), attributes);
        if (result->eglImage == EGL_NO_IMAGE_KHR)
            return Error{ErrorCode::Unsupported, "EGL rejected AHB image import"};
        auto &gl = context->gl;
        gl.GenTextures(1, &result->object);
        gl.BindTexture(result->target, result->object);
        gl.TexParameteri(result->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(result->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(result->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(result->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (result->storageImage)
            gl.ImageTargetStorage(result->target, result->eglImage, nullptr);
        else
            gl.ImageTargetTexture(result->target, result->eglImage);
        auto checked = context->error("bind AHB image");
        if (!checked)
            return checked.error();
        return result;
    }
#else
    (void)context;
#endif
    return Error{ErrorCode::Unsupported,
                 "GL native image import requires a supported native descriptor"};
}
} // namespace lutils::compute::gl_detail
