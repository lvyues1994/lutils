#include <algorithm>
#include <cstring>
#include <limits>
#include <lutils/image/CpuConversionExecutor.hpp>
#include <lutils/image/ImageResource.hpp>

namespace lutils::image {
namespace {
using compute::Access;
bool aliases(compute::Memory const &a, compute::Memory const &b) {
    return &a == &b || a.aliases(b) || b.aliases(a);
}
struct MappedImage {
    ConstFrameView read;
    FrameView view;
    std::vector<std::unique_ptr<compute::MemoryMapping>> mappings;
    std::unique_ptr<compute::NativeImageMapping> native;
    std::vector<compute::NativePlane> samples;
    std::vector<std::byte *> writes;
    Result<void> finish() {
        std::optional<Error> failure;
        if (native) {
            auto done = native->finish();
            if (!done)
                failure = done.error();
        }
        for (auto &m : mappings) {
            auto done = m->finish();
            if (!done && !failure)
                failure = done.error();
        }
        return failure ? Result<void>{*failure} : Result<void>{};
    }
};
Result<MappedImage> mapImage(ImageResource const &image, Access access) {
    MappedImage result;
    result.view.desc = image.description();
    result.read.desc = image.description();
    if (image.nativeStorage()) {
        auto mapped = image.nativeStorage()->map(access);
        if (!mapped)
            return mapped.error();
        result.native = std::move(mapped).value();
        auto const crop = image.visibleRectangle();
        if (!result.native ||
            result.native->planes().size() != image.description().format.planes.size())
            return Error{ErrorCode::InvalidArgument, "native mapping plane count mismatch"};
        auto const allocation = image.nativeStorage()->description();
        for (std::size_t p = 0; p < result.native->planes().size(); ++p) {
            auto v = result.native->planes()[p];
            auto const &f = image.description().format.planes[p];
            auto const columns = allocation.width / f.blockWidth;
            auto const rows = allocation.height / f.blockHeight;
            if (!v.data || v.capacity > std::size_t{PTRDIFF_MAX} || v.capacity < f.blockBytes ||
                v.pixelStride < f.blockBytes || !v.rowStride ||
                columns - 1 > (v.capacity - f.blockBytes) / v.pixelStride)
                return Error{ErrorCode::Bounds, "invalid native mapping pixel span"};
            auto rowBytes = (columns - 1) * v.pixelStride + f.blockBytes;
            if (rowBytes > v.rowStride || rows - 1 > (v.capacity - rowBytes) / v.rowStride)
                return Error{ErrorCode::Bounds, "native mapping exceeds plane capacity"};
            auto const offset =
                (crop.y / f.blockHeight) * v.rowStride + (crop.x / f.blockWidth) * v.pixelStride;
            v.data += offset;
            v.capacity -= offset;
            result.samples.push_back(v);
            if (access != Access::Read) {
                auto writable = result.native->writablePlane(p);
                if (!writable)
                    return writable.error();
                if (!writable.value())
                    return Error{ErrorCode::InvalidArgument, "null native writable plane"};
                result.writes.push_back(writable.value() + offset);
            }
        }
        return result;
    }
    for (auto const &object : image.objects()) {
        auto mapping = object->map(access);
        if (!mapping)
            return mapping.error();
        result.mappings.push_back(std::move(mapping).value());
    }
    for (auto const &p : image.planes()) {
        result.read.planes.push_back({result.mappings[p.object]->data(),
                                      image.objects()[p.object]->byteCount(), p.offset, p.stride});
        if (access != Access::Read) {
            auto data = result.mappings[p.object]->writableData();
            if (!data)
                return data.error();
            result.view.planes.push_back(
                {data.value(), image.objects()[p.object]->byteCount(), p.offset, p.stride});
        }
    }
    return result;
}
Result<void> runMapped(RegionPlan const &plan, MappedImage const &input,
                       MappedImage const &output) {
    if (!input.native && !output.native)
        return plan.run(input.read, output.view);
    // Native planes can be interleaved. Copy logical blocks only, never row
    // padding.
    for (std::size_t p = 0; p < plan.planes().size(); ++p) {
        auto const &region = plan.planes()[p];
        auto const bytes = plan.source().format.planes[p].blockBytes;
        auto const src = [&](std::size_t const x, std::size_t const y) {
            return input.native ? input.samples[p].data + y * input.samples[p].rowStride +
                                      x * input.samples[p].pixelStride
                                : row(input.read.planes[p], y) + x * bytes;
        };
        auto const dst = [&](std::size_t const x, std::size_t const y) {
            return output.native ? output.writes[p] + y * output.samples[p].rowStride +
                                       x * output.samples[p].pixelStride
                                 : row(output.view.planes[p], y) + x * bytes;
        };
        for (std::size_t y = 0; y < region.rows; ++y)
            for (std::size_t x = 0; x < region.rowBytes / bytes; ++x) {
                auto const left = region.destinationX / bytes;
                if (x >= left && x - left < region.copyBytes / bytes && y >= region.destinationY &&
                    y - region.destinationY < region.copyRows)
                    std::memcpy(dst(x, y),
                                src(region.sourceX / bytes + x - left,
                                    region.sourceY + y - region.destinationY),
                                bytes);
                else if (!region.background.empty())
                    std::memcpy(dst(x, y), region.background.data(), bytes);
            }
    }
    return {};
}
struct Completed final : compute::Completion {
    Result<void> wait() override { return {}; }
    Result<bool> ready() override { return true; }
};
Result<void> runMapped(CpuConversionPlan const &plan, MappedImage const &input,
                       MappedImage &output) {
    if (input.native)
        return Error{ErrorCode::Unsupported, "CPU conversion requires linear UYVY or NV12 input"};
    if (output.native) {
        if (output.view.desc.format != rgba8() || output.samples.size() != 1 ||
            output.samples[0].pixelStride != 4 || output.samples[0].rowStride > PTRDIFF_MAX)
            return Error{ErrorCode::Unsupported,
                         "CPU conversion requires packed native RGBA output"};
        auto const &p = output.samples[0];
        output.view.planes.push_back(
            {output.writes[0], p.capacity, 0, static_cast<std::ptrdiff_t>(p.rowStride)});
    }
    return plan.run(input.read, output.view);
}
struct CpuConversions final : CpuConversionExecutor {
    Result<std::shared_ptr<compute::Completion>> submit(CpuConversionPlan const &plan,
                                                        ImageResource const &source,
                                                        ImageResource const &destination) override {
        if (!(source.description() == plan.source()) ||
            !(destination.description() == plan.destination()) || source.aliases(destination))
            return Error{ErrorCode::InvalidArgument, "conversion resources mismatch or alias"};
        auto input = mapImage(source, Access::Read);
        if (!input)
            return input.error();
        auto output = mapImage(destination, Access::Write);
        if (!output) {
            (void)input.value().finish();
            return output.error();
        }
        auto result = runMapped(plan, input.value(), output.value());
        auto out = output.value().finish();
        auto in = input.value().finish();
        if (!result)
            return result.error();
        if (!out)
            return out.error();
        if (!in)
            return in.error();
        return std::shared_ptr<compute::Completion>{std::make_shared<Completed>()};
    }
};
struct CpuRegions final : RegionExecutor {
    Result<std::shared_ptr<compute::Completion>> submit(RegionPlan const &plan,
                                                        ImageResource const &source,
                                                        ImageResource const &destination) override {
        if (!(source.description() == plan.source()) ||
            !(destination.description() == plan.destination()) || source.aliases(destination))
            return Error{ErrorCode::InvalidArgument, "region resources mismatch or alias"};
        auto input = mapImage(source, Access::Read);
        if (!input)
            return input.error();
        auto output = mapImage(destination, Access::Write);
        if (!output)
            return output.error();
        auto result = runMapped(plan, input.value(), output.value());
        auto out = output.value().finish();
        auto in = input.value().finish();
        if (!result)
            return result.error();
        if (!out)
            return out.error();
        if (!in)
            return in.error();
        return std::shared_ptr<compute::Completion>{std::make_shared<Completed>()};
    }
};
template <class View> Result<ImageResource> borrowView(View const &view) {
    auto valid = validate(view);
    if (!valid)
        return valid.error();
    std::vector<compute::MemoryHandle> objects;
    std::vector<PlaneLayout> planes;
    for (std::size_t i = 0; i < view.planes.size(); ++i) {
        auto const &p = view.planes[i];
        std::size_t j = 0;
        for (; j < i; ++j)
            if (view.planes[j].base == p.base && view.planes[j].capacity == p.capacity)
                break;
        std::size_t index;
        if (j < i)
            index = planes[j].object;
        else {
            auto memory = compute::borrowMemory(p.base, p.capacity);
            if (!memory)
                return memory.error();
            index = objects.size();
            objects.push_back(std::move(memory).value());
        }
        planes.push_back({index, p.row0, p.stride});
    }
    return ImageResource::linear(view.desc, std::move(objects), std::move(planes));
}
} // namespace
Result<ImageResource> ImageResource::linear(FrameDesc description,
                                            std::vector<compute::MemoryHandle> objects,
                                            std::vector<PlaneLayout> planes) {
    auto shape = geometry(description);
    if (!shape)
        return shape.error();
    if (!description.width || !description.height || planes.size() != shape.value().size() ||
        objects.empty())
        return Error{ErrorCode::InvalidArgument,
                     "linear image requires nonempty dimensions and planes"};
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i] || !objects[i]->byteCount() ||
            objects[i]->byteCount() > std::size_t{PTRDIFF_MAX})
            return Error{ErrorCode::InvalidArgument, "invalid image memory object"};
        for (std::size_t j = 0; j < i; ++j)
            if (::lutils::image::aliases(*objects[i], *objects[j]))
                return Error{ErrorCode::InvalidArgument,
                             "aliased objects must use one canonical memory object"};
        if (std::none_of(planes.begin(), planes.end(),
                         [&](auto const &p) { return p.object == i; }))
            return Error{ErrorCode::InvalidArgument, "unused image memory object"};
    }
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (std::size_t i = 0; i < planes.size(); ++i) {
        auto const &p = planes[i];
        auto g = shape.value()[i];
        if (p.object >= objects.size())
            return Error{ErrorCode::InvalidArgument, "plane references a missing memory object"};
        auto capacity = objects[p.object]->byteCount();
        auto step = p.stride < 0 ? static_cast<std::size_t>(-(p.stride + 1)) + 1
                                 : static_cast<std::size_t>(p.stride);
        if (p.offset > capacity ||
            (g.rows > 1 && (step < g.rowBytes || step > capacity / (g.rows - 1))))
            return Error{ErrorCode::Bounds, "plane stride or offset exceeds storage"};
        auto delta = step * (g.rows - 1);
        if ((p.stride < 0 && delta > p.offset) || (p.stride >= 0 && delta > capacity - p.offset))
            return Error{ErrorCode::Bounds, "plane rows exceed storage"};
        auto begin = p.stride < 0 ? p.offset - delta : p.offset;
        auto last = p.stride < 0 ? p.offset : p.offset + delta;
        if (g.rowBytes > capacity - last)
            return Error{ErrorCode::Bounds, "plane row exceeds storage"};
        auto end = last + g.rowBytes;
        for (std::size_t j = 0; j < i; ++j)
            if (planes[j].object == p.object && begin < spans[j].second && spans[j].first < end)
                return Error{ErrorCode::InvalidArgument, "plane address spans overlap"};
        spans.emplace_back(begin, end);
    }
    ImageResource result;
    result.description_ = std::move(description);
    result.objects_ = std::move(objects);
    result.planes_ = std::move(planes);
    return result;
}
Result<ImageResource> ImageResource::borrow(ConstFrameView const &view) { return borrowView(view); }
Result<ImageResource> ImageResource::borrow(FrameView const &view) { return borrowView(view); }
Result<ImageResource> ImageResource::native(std::shared_ptr<compute::NativeImage> image,
                                            ColorSpec color, Rectangle visible) {
    if (!image)
        return Error{ErrorCode::InvalidArgument, "null native image"};
    auto const d = image->description();
    if ((d.format != compute::NativeImageFormat::Rgba8 &&
         d.format != compute::NativeImageFormat::Yuv420) ||
        !d.width || !d.height ||
        (d.format == compute::NativeImageFormat::Yuv420 && (d.width % 2 || d.height % 2)))
        return Error{ErrorCode::InvalidArgument, "invalid native image description"};
    if (!visible.x && !visible.y && !visible.width && !visible.height)
        visible = {0, 0, d.width, d.height};
    if (!visible.width || !visible.height || visible.x > d.width || visible.y > d.height ||
        visible.width > d.width - visible.x || visible.height > d.height - visible.y)
        return Error{ErrorCode::Bounds, "native image crop exceeds allocation"};
    auto format = d.format == compute::NativeImageFormat::Rgba8 ? rgba8() : i420();
    for (auto const &p : format.planes)
        if (visible.x % p.blockWidth || visible.width % p.blockWidth || visible.y % p.blockHeight ||
            visible.height % p.blockHeight)
            return Error{ErrorCode::InvalidArgument, "native crop is not sample aligned"};
    ImageResource result;
    result.description_ = {visible.width, visible.height, std::move(format), color};
    result.native_ = std::move(image);
    result.visible_ = visible;
    return result;
}
bool ImageResource::aliases(ImageResource const &other) const noexcept {
    if (native_ && other.native_)
        return native_ == other.native_ || native_->aliases(*other.native_) ||
               other.native_->aliases(*native_);
    for (auto const &a : objects_)
        for (auto const &b : other.objects_)
            if (::lutils::image::aliases(*a, *b))
                return true;
    return false;
}
Result<void> RegionExecutor::run(RegionPlan const &plan, ImageResource const &source,
                                 ImageResource const &destination) {
    auto result = submit(plan, source, destination);
    return result ? result.value()->wait() : Result<void>{result.error()};
}
std::unique_ptr<RegionExecutor> createCpuRegionExecutor() { return std::make_unique<CpuRegions>(); }
Result<void> CpuConversionExecutor::run(CpuConversionPlan const &plan, ImageResource const &source,
                                        ImageResource const &destination) {
    auto result = submit(plan, source, destination);
    return result ? result.value()->wait() : Result<void>{result.error()};
}
std::unique_ptr<CpuConversionExecutor> createCpuConversionExecutor() {
    return std::make_unique<CpuConversions>();
}
Result<ImageResource> bindImage(compute::Device &device, ImageResource const &image) {
    if (image.native_) {
        if (image.nativeBinding_)
            return image;
        auto binding = device.importImage(image.native_);
        if (!binding)
            return binding.error();
        auto result = image;
        result.nativeBinding_ = std::move(binding).value();
        return result;
    }
    std::vector<compute::MemoryHandle> objects;
    for (auto const &m : image.objects()) {
        auto buffer = device.importMemory(m);
        if (!buffer)
            return buffer.error();
        auto memory = compute::bufferMemory(std::move(buffer).value());
        if (!memory)
            return memory.error();
        objects.push_back(std::move(memory).value());
    }
    return ImageResource::linear(image.description(), std::move(objects), image.planes());
}
} // namespace lutils::image
