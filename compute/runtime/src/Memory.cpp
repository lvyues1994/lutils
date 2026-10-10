#include <limits>
#include <lutils/compute/Memory.hpp>

namespace lutils::compute {
namespace {
struct HostMapping final : MemoryMapping {
    std::byte const *base;
    bool writable;
    std::shared_ptr<void> owner;
    HostMapping(std::byte const *p, bool write, std::shared_ptr<void> keep)
        : base(p), writable(write), owner(std::move(keep)) {}
    std::byte const *data() const noexcept override { return base; }
    Result<std::byte *> writableData() override {
        if (!writable)
            return Error{ErrorCode::InvalidArgument, "mapping is read-only"};
        return const_cast<std::byte *>(base);
    }
    Result<void> finish() override { return {}; }
};
struct HostMemory final : Memory {
    std::byte const *base;
    std::size_t bytes;
    bool write;
    std::shared_ptr<void> owner;
    HostMemory(std::byte const *p, std::size_t n, bool w, std::shared_ptr<void> keep)
        : base(p), bytes(n), write(w), owner(std::move(keep)) {}
    std::size_t byteCount() const noexcept override { return bytes; }
    bool writable() const noexcept override { return write; }
    bool aliases(Memory const &other) const noexcept override {
        auto const *b = dynamic_cast<HostMemory const *>(&other);
        if (!b)
            return false;
        auto x = reinterpret_cast<std::uintptr_t>(base);
        auto y = reinterpret_cast<std::uintptr_t>(b->base);
        return x <= y ? y - x < bytes : x - y < b->bytes;
    }
    Result<std::unique_ptr<MemoryMapping>> map(Access access) override {
        if (access != Access::Read && !write)
            return Error{ErrorCode::InvalidArgument, "host memory is read-only"};
        return std::unique_ptr<MemoryMapping>{
            std::make_unique<HostMapping>(base, access != Access::Read, owner)};
    }
};
struct ResidentMemory final : Memory {
    BufferHandle buffer;
    explicit ResidentMemory(BufferHandle b) : buffer(std::move(b)) {}
    std::size_t byteCount() const noexcept override { return buffer->byteCount(); }
    bool writable() const noexcept override { return buffer->writable(); }
    bool aliases(Memory const &other) const noexcept override {
        auto b = other.residentBuffer();
        return b && (buffer->aliases(*b) || b->aliases(*buffer));
    }
    BufferHandle residentBuffer() const override { return buffer; }
    Result<std::unique_ptr<MemoryMapping>> map(Access) override {
        return Error{ErrorCode::Unsupported, "resident buffer does not expose a CPU mapping"};
    }
};
Result<MemoryHandle> host(std::byte const *p, std::size_t n, bool write,
                          std::shared_ptr<void> keep) {
    if (!p || !n ||
        n > std::numeric_limits<std::uintptr_t>::max() - reinterpret_cast<std::uintptr_t>(p))
        return Error{ErrorCode::InvalidArgument, "invalid host allocation"};
    return MemoryHandle{std::make_shared<HostMemory>(p, n, write, std::move(keep))};
}
} // namespace
Result<MemoryHandle> borrowMemory(std::byte *p, std::size_t n, std::shared_ptr<void> keep) {
    return host(p, n, true, std::move(keep));
}
Result<MemoryHandle> borrowMemory(std::byte const *p, std::size_t n, std::shared_ptr<void> keep) {
    return host(p, n, false, std::move(keep));
}
Result<MemoryHandle> bufferMemory(BufferHandle buffer) {
    if (!buffer || buffer->imageDescription())
        return Error{ErrorCode::InvalidArgument, "linear memory requires a buffer"};
    return MemoryHandle{std::make_shared<ResidentMemory>(std::move(buffer))};
}
Result<BufferHandle> Device::importMemory(MemoryHandle const &memory) {
    if (!memory)
        return Error{ErrorCode::InvalidArgument, "null memory resource"};
    if (auto buffer = memory->residentBuffer())
        return buffer;
    return Error{ErrorCode::Unsupported, "device cannot directly import this memory resource"};
}
} // namespace lutils::compute
