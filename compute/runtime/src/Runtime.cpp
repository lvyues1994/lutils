#include <limits>
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
Result<std::size_t> imageWordCount(ImageDesc const &desc) {
    auto e = desc.extent;
    if (!e.x || !e.y || !e.z || desc.dimensions < 1 || desc.dimensions > 3 ||
        (desc.dimensions == 1 && e.y != 1) || (desc.dimensions < 3 && e.z != 1))
        return Error{ErrorCode::InvalidArgument, "invalid image dimensions"};
    if (e.x > INT32_MAX || e.y > INT32_MAX || e.z > INT32_MAX)
        return Error{ErrorCode::Unsupported, "image coordinates exceed signed 32-bit indexing"};
    std::size_t bytes;
    try {
        bytes = kernel::formatInfo(desc.format).bytes;
    } catch (std::invalid_argument const &e) {
        return Error{ErrorCode::InvalidArgument, e.what()};
    }
    for (auto value : {e.x, e.y, e.z}) {
        if (value > (std::numeric_limits<std::size_t>::max() - 3) / bytes)
            return Error{ErrorCode::Overflow, "image size overflow"};
        bytes *= value;
    }
    if ((bytes + 3) / 4 > UINT32_MAX)
        return Error{ErrorCode::Unsupported, "image storage exceeds 32-bit indexing"};
    return (bytes + 3) / 4;
}
Result<void> CommandList::upload(BufferHandle buffer, std::vector<Word> words) {
    if (!buffer || !buffer->writable() || buffer->wordCount() != words.size())
        return Error{ErrorCode::InvalidArgument, "upload buffer or size mismatch"};
    commands_.push_back(
        Upload{std::move(buffer), std::make_shared<std::vector<Word> const>(std::move(words))});
    return {};
}
Result<ReadbackToken> CommandList::readback(BufferHandle buffer) {
    if (!buffer)
        return Error{ErrorCode::InvalidArgument, "null readback buffer"};
    ReadbackToken token;
    token.identity_ = std::make_shared<ReadbackToken::Identity>();
    commands_.push_back(Readback{std::move(buffer), token});
    return token;
}
Result<void> validate(KernelSource const &source) {
    if (source.abiVersion != 1 && source.abiVersion != 2)
        return Error{ErrorCode::Unsupported, "unsupported kernel ABI"};
    if (source.name.empty() || source.bindings.empty() || !source.localSize.x ||
        !source.localSize.y || !source.localSize.z)
        return Error{ErrorCode::InvalidArgument, "kernel requires name, bindings and local size"};
    if (source.parameterWords > 32)
        return Error{ErrorCode::Unsupported, "kernel parameters exceed the 128-byte ABI limit"};
    if (source.abiVersion == 1 && (source.localSize.y != 1 || source.localSize.z != 1))
        return Error{ErrorCode::Unsupported, "kernel ABI v1 requires local size Y=Z=1"};
    if (source.abiVersion == 2 &&
        (source.resources.size() != source.bindings.size() || source.parameterWords < 4))
        return Error{ErrorCode::InvalidArgument,
                     "ABI v2 requires resource metadata and dispatch extent"};
    if (!source.resources.empty() && source.resources.size() != source.bindings.size())
        return Error{ErrorCode::InvalidArgument, "resource metadata count mismatch"};
    for (auto const &resource : source.resources) {
        if (!resource.elementWords)
            return Error{ErrorCode::InvalidArgument, "resource element stride must be positive"};
        if (resource.image) {
            auto valid = imageWordCount(*resource.image);
            if (!valid)
                return valid.error();
        }
    }
    return {};
}
Result<void> CommandList::dispatch(Dispatch command) {
    if (!command.kernel)
        return Error{ErrorCode::InvalidArgument, "null kernel"};
    auto const &source = command.kernel->source();
    auto valid = validate(source);
    if (!valid)
        return valid;
    if (command.buffers.size() != source.bindings.size() ||
        command.parameters.size() != source.parameterWords)
        return Error{ErrorCode::InvalidArgument, "kernel binding or parameter count mismatch"};
    for (std::size_t i = 0; i < command.buffers.size(); ++i) {
        if (!command.buffers[i])
            return Error{ErrorCode::InvalidArgument, "null buffer"};
        if (source.bindings[i] != Access::Read && !command.buffers[i]->writable())
            return Error{ErrorCode::InvalidArgument, "writable binding uses read-only storage"};
        auto image = command.buffers[i]->imageDescription();
        auto const *info = source.resources.empty() ? nullptr : &source.resources[i];
        if (image) {
            if (!info || !info->image || info->image->format != image->format ||
                info->image->dimensions != image->dimensions)
                return Error{ErrorCode::InvalidArgument,
                             "image binding format or dimension mismatch"};
        } else if (info &&
                   (info->image ||
                    (info->elementBytes ? command.buffers[i]->byteCount() % info->elementBytes
                                        : command.buffers[i]->wordCount() % info->elementWords))) {
            return Error{ErrorCode::InvalidArgument, "buffer binding kind or stride mismatch"};
        }
        for (std::size_t j = 0; j < i; ++j) {
            if ((command.buffers[i]->aliases(*command.buffers[j]) ||
                 command.buffers[j]->aliases(*command.buffers[i])) &&
                (source.bindings[i] != Access::Read || source.bindings[j] != Access::Read))
                return Error{ErrorCode::InvalidArgument, "writable bindings must not alias"};
        }
    }
    auto aligned = [](Word extent, Word local) { return extent % local == 0; };
    // Exact grids avoid executing extra invocations outside the CPU domain.
    if ((source.abiVersion == 1 || source.requiresFullWorkgroups) &&
        (!aligned(command.extent.x, source.localSize.x) ||
         !aligned(command.extent.y, source.localSize.y) ||
         !aligned(command.extent.z, source.localSize.z)))
        return Error{ErrorCode::InvalidArgument, "dispatch extent must be divisible by local size"};
    if (source.abiVersion == 2 &&
        (command.parameters.size() < 4 || command.parameters[0] != command.extent.x ||
         command.parameters[1] != command.extent.y || command.parameters[2] != command.extent.z))
        return Error{ErrorCode::InvalidArgument, "ABI v2 dispatch extent parameters mismatch"};
    commands_.push_back(std::move(command));
    return {};
}
} // namespace lutils::compute
