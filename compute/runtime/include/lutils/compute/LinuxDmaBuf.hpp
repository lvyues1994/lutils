#pragma once
#include <lutils/compute/ExternalMemory.hpp>

namespace lutils::compute {
struct DmaBufMemory : ExternalMemory {
    // Borrows fd, owns a CLOEXEC duplicate. Queries the actual allocation size.
    static Result<std::shared_ptr<DmaBufMemory>> import(int fd);
    virtual int fd() const noexcept = 0;
    virtual Result<void> wait(Access access) const = 0;
};
} // namespace lutils::compute
