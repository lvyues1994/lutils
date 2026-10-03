#include <limits>
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
Result<void> CommandList::upload(BufferHandle buffer, std::vector<Word> words) {
    if (!buffer || buffer->wordCount() != words.size())
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
    if (source.abiVersion != 1)
        return Error{ErrorCode::Unsupported, "unsupported kernel ABI"};
    if (source.name.empty() || source.bindings.empty() || !source.localSize.x ||
        !source.localSize.y || !source.localSize.z)
        return Error{ErrorCode::InvalidArgument, "kernel requires name, bindings and local size"};
    if (source.parameterWords > 32)
        return Error{ErrorCode::Unsupported, "kernel parameters exceed the 128-byte ABI limit"};
    if (source.localSize.y != 1 || source.localSize.z != 1)
        return Error{ErrorCode::Unsupported, "kernel ABI v1 requires local size Y=Z=1"};
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
        for (std::size_t j = 0; j < i; ++j) {
            if (command.buffers[i] == command.buffers[j] &&
                (source.bindings[i] == Access::Write || source.bindings[j] == Access::Write))
                return Error{ErrorCode::InvalidArgument, "writable bindings must not alias"};
        }
    }
    auto aligned = [](Word extent, Word local) { return extent % local == 0; };
    // Exact grids avoid executing extra invocations outside the CPU domain.
    if (!aligned(command.extent.x, source.localSize.x) ||
        !aligned(command.extent.y, source.localSize.y) ||
        !aligned(command.extent.z, source.localSize.z))
        return Error{ErrorCode::InvalidArgument, "dispatch extent must be divisible by local size"};
    commands_.push_back(std::move(command));
    return {};
}
} // namespace lutils::compute
