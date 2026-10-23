#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/sync_file.h>
#include <lutils/compute/LinuxDmaBufImage.hpp>
#include <sys/ioctl.h>

namespace lutils::compute {
namespace {
constexpr Word fourcc(char a, char b, char c, char d) {
    return Word(a) | (Word(b) << 8) | (Word(c) << 16) | (Word(d) << 24);
}
constexpr auto rgba = fourcc('A', 'B', '2', '4');
constexpr auto nv12 = fourcc('N', 'V', '1', '2');
constexpr auto nv21 = fourcc('N', 'V', '2', '1');
constexpr auto yuv420 = fourcc('Y', 'U', '1', '2');
struct Mapping final : NativeImageMapping {
    std::vector<std::unique_ptr<MemoryMapping>> mappings;
    std::vector<NativePlane> values;
    bool write = false, finished = false;
    ~Mapping() override { (void)finish(); }
    std::vector<NativePlane> const &planes() const noexcept override { return values; }
    Result<std::byte *> writablePlane(std::size_t plane) override {
        if (!write || finished || plane >= values.size())
            return Error{ErrorCode::InvalidArgument, "native dma-buf plane is not writable"};
        return const_cast<std::byte *>(values[plane].data);
    }
    Result<void> finish() override {
        if (finished)
            return {};
        // No plane may remain accessible after any END has succeeded. Keep
        // mappings only to retry cleanup for objects whose END failed.
        write = false;
        values.clear();
        std::optional<Error> error;
        for (auto &mapping : mappings) {
            auto done = mapping->finish();
            if (!done && !error)
                error = done.error();
        }
        if (error)
            return *error;
        values.clear();
        mappings.clear();
        finished = true;
        return {};
    }
};
struct Image final : DmaBufNativeImage {
    DmaBufNativeImageDesc desc;
    std::vector<std::shared_ptr<DmaBufMemory>> objects;
    std::vector<std::size_t> indices;
    DmaBufNativeImageDesc const &dmaDescription() const noexcept override { return desc; }
    NativeImageDesc description() const noexcept override { return desc.image; }
    bool writable() const noexcept override { return !desc.readOnly; }
    bool aliases(NativeImage const &other) const noexcept override {
        auto image = dynamic_cast<Image const *>(&other);
        if (!image)
            return false;
        for (auto const &a : objects)
            for (auto const &b : image->objects)
                if (a->aliases(*b))
                    return true;
        return false;
    }
    bool aliasesMemory(Memory const &other) const noexcept override {
        for (auto const &object : objects)
            if (object->aliases(other))
                return true;
        return false;
    }
    Result<FileDescriptor> exportSyncFile(Access access) const override {
        FileDescriptor result;
        for (auto const &object : objects) {
            auto exported = object->exportSyncFile(access);
            if (!exported)
                return exported.error();
            if (result.get() < 0) {
                result = std::move(exported).value();
                continue;
            }
            sync_merge_data data{};
            std::strcpy(data.name, "lutils-dma-image");
            data.fd2 = exported.value().get();
            int status;
            do {
#ifdef __ANDROID__
                status = ioctl(result.get(), static_cast<unsigned>(SYNC_IOC_MERGE), &data);
#else
                status = ioctl(result.get(), SYNC_IOC_MERGE, &data);
#endif
            } while (status < 0 && errno == EINTR);
            if (status < 0)
                return Error{ErrorCode::Device, "merge dma-buf acquire fences failed"};
            result = FileDescriptor{data.fence};
        }
        return result;
    }
    Result<void> importSyncFile(int fd, Access access) const override {
        std::optional<Error> failure;
        for (auto const &object : objects) {
            auto done = object->importSyncFile(fd, access);
            if (!done && !failure)
                failure = done.error();
        }
        return failure ? Result<void>{*failure} : Result<void>{};
    }
    Result<std::unique_ptr<NativeImageMapping>> map(Access access) override {
        if (access != Access::Read && !writable())
            return Error{ErrorCode::InvalidArgument, "native dma-buf is read-only"};
        for (auto const &plane : desc.planes)
            if (plane.modifier)
                return Error{ErrorCode::Unsupported, "CPU mapping requires linear dma-buf images"};
        auto mapping = std::make_unique<Mapping>();
        mapping->write = access != Access::Read;
        for (auto const &object : objects) {
            auto mapped = object->map(access);
            if (!mapped)
                return mapped.error();
            mapping->mappings.push_back(std::move(mapped).value());
        }
        auto add = [&](std::size_t p, std::size_t offset, std::size_t pixel) {
            auto const &plane = desc.planes[p];
            auto i = indices[p];
            mapping->values.push_back({mapping->mappings[i]->data() + plane.offset + offset,
                                       plane.stride, pixel,
                                       objects[i]->byteCount() - plane.offset - offset});
        };
        if (desc.fourcc == rgba)
            add(0, 0, 4);
        else {
            add(0, 0, 1);
            if (desc.fourcc == yuv420) {
                add(1, 0, 1);
                add(2, 0, 1);
            } else {
                add(1, desc.fourcc == nv12 ? 0u : 1u, 2);
                add(1, desc.fourcc == nv12 ? 1u : 0u, 2);
            }
        }
        return std::unique_ptr<NativeImageMapping>{std::move(mapping)};
    }
};
} // namespace
Result<std::shared_ptr<DmaBufNativeImage>> DmaBufNativeImage::import(DmaBufNativeImageDesc desc) {
    bool rgb = desc.fourcc == rgba;
    if (!rgb && desc.fourcc != nv12 && desc.fourcc != nv21 && desc.fourcc != yuv420)
        return Error{ErrorCode::Unsupported, "unsupported native dma-buf FourCC"};
    auto expected = rgb ? NativeImageFormat::Rgba8 : NativeImageFormat::Yuv420;
    if (desc.image.format != expected || !desc.image.width || !desc.image.height ||
        desc.image.width > INT32_MAX || desc.image.height > INT32_MAX ||
        desc.planes.size() != (rgb                     ? 1u
                               : desc.fourcc == yuv420 ? 3u
                                                       : 2u))
        return Error{ErrorCode::InvalidArgument,
                     "native dma-buf dimensions, format or plane count mismatch"};
    auto result = std::make_shared<Image>();
    for (std::size_t p = 0; p < desc.planes.size(); ++p) {
        auto &plane = desc.planes[p];
        if (plane.modifier == 0x00ffffffffffffffULL)
            return Error{ErrorCode::InvalidArgument,
                         "implicit DRM modifier is not an explicit layout"};
        if (!plane.stride || plane.stride > INT32_MAX || plane.offset > INT32_MAX)
            return Error{ErrorCode::InvalidArgument, "invalid dma-buf plane pitch/offset"};
        auto imported = DmaBufMemory::import(plane.fd);
        if (!imported)
            return imported.error();
        auto memory = std::move(imported).value();
        auto width =
            p == 0 ? std::uint64_t{desc.image.width} : (std::uint64_t{desc.image.width} + 1) / 2;
        auto height =
            p == 0 ? std::uint64_t{desc.image.height} : (std::uint64_t{desc.image.height} + 1) / 2;
        auto rowBytes = width * (rgb ? 4u : p == 0 || desc.fourcc == yuv420 ? 1u : 2u);
        if (plane.offset >= memory->byteCount() ||
            (!plane.modifier &&
             (plane.stride < rowBytes ||
              plane.offset + (height - 1) * plane.stride + rowBytes > memory->byteCount())))
            return Error{ErrorCode::Bounds, "native dma-buf plane exceeds its allocation"};
        desc.readOnly = desc.readOnly || !memory->writable();
        std::size_t index = 0;
        while (index < result->objects.size() && !memory->aliases(*result->objects[index]))
            ++index;
        if (index == result->objects.size())
            result->objects.push_back(memory);
        if (!plane.modifier) {
            auto end = plane.offset + (height - 1) * plane.stride + rowBytes;
            for (std::size_t previous = 0; previous < p; ++previous) {
                auto const &old = desc.planes[previous];
                if (result->indices[previous] != index || old.modifier)
                    continue;
                auto oldHeight = previous == 0 ? std::uint64_t{desc.image.height}
                                               : (std::uint64_t{desc.image.height} + 1) / 2;
                auto oldWidth = previous == 0 ? std::uint64_t{desc.image.width}
                                              : (std::uint64_t{desc.image.width} + 1) / 2;
                auto oldBytes = oldWidth * (rgb                                      ? 4u
                                            : previous == 0 || desc.fourcc == yuv420 ? 1u
                                                                                     : 2u);
                auto oldEnd = old.offset + (oldHeight - 1) * old.stride + oldBytes;
                if (plane.offset < oldEnd && old.offset < end)
                    return Error{ErrorCode::InvalidArgument, "native dma-buf planes overlap"};
            }
        }
        result->indices.push_back(index);
        plane.fd = result->objects[index]->fd();
    }
    result->desc = std::move(desc);
    return std::shared_ptr<DmaBufNativeImage>{std::move(result)};
}
} // namespace lutils::compute
