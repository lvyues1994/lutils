#pragma once
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
struct MemoryMapping {
    virtual ~MemoryMapping() = default;
    virtual std::byte const *data() const noexcept = 0;
    virtual Result<std::byte *> writableData() = 0;
    // Ends access and reports synchronization failures. Destruction is a cleanup fallback.
    virtual Result<void> finish() = 0;
};
struct Memory {
    virtual ~Memory() = default;
    virtual std::size_t byteCount() const noexcept = 0;
    virtual bool writable() const noexcept = 0;
    virtual bool aliases(Memory const &other) const noexcept = 0;
    virtual Result<std::unique_ptr<MemoryMapping>> map(Access access) = 0;
    virtual BufferHandle residentBuffer() const { return {}; }
};
// The optional owner keeps an allocation alive; otherwise the caller owns its lifetime.
// A mapped dma-buf must use its dma-buf resource, not these ordinary host factories.
Result<MemoryHandle> borrowMemory(std::byte *base, std::size_t bytes,
                                  std::shared_ptr<void> owner = {});
Result<MemoryHandle> borrowMemory(std::byte const *base, std::size_t bytes,
                                  std::shared_ptr<void> owner = {});
Result<MemoryHandle> bufferMemory(BufferHandle buffer);
} // namespace lutils::compute
