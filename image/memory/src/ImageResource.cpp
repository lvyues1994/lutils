#include <algorithm>
#include <limits>
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
    Result<void> finish() {
        std::optional<Error> failure;
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
struct Completed final : compute::Completion {
    Result<void> wait() override { return {}; }
    Result<bool> ready() override { return true; }
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
        auto output = mapImage(destination, Access::ReadWrite);
        if (!output)
            return output.error();
        auto result = plan.run(input.value().read, output.value().view);
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
bool ImageResource::aliases(ImageResource const &other) const noexcept {
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
Result<ImageResource> bindImage(compute::Device &device, ImageResource const &image) {
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
