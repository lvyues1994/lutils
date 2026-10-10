#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/image/LinuxImage.hpp>

namespace lutils::image {
Result<ImageResource> importDmaBufImage(DmaBufImageDesc const &description) {
    if (!description.modifier || *description.modifier != 0)
        return Error{ErrorCode::Unsupported, "dma-buf regions require explicit linear layout"};
    std::vector<compute::MemoryHandle> objects;
    std::vector<std::size_t> indices;
    for (auto fd : description.fds) {
        auto memory = compute::DmaBufMemory::import(fd);
        if (!memory)
            return memory.error();
        std::size_t i = 0;
        for (; i < objects.size(); ++i)
            if (objects[i]->aliases(*memory.value()))
                break;
        if (i == objects.size())
            objects.push_back(memory.value());
        indices.push_back(i);
    }
    auto planes = description.planes;
    for (auto &p : planes) {
        if (p.object >= indices.size())
            return Error{ErrorCode::InvalidArgument, "plane references a missing dma-buf"};
        p.object = indices[p.object];
    }
    return ImageResource::linear(description.image, std::move(objects), std::move(planes));
}
} // namespace lutils::image
