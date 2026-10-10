#pragma once
#include <lutils/compute/FileDescriptor.hpp>
#include <lutils/compute/Memory.hpp>

namespace lutils::compute {
// Access is serialized by the caller. Export owns its fd; import borrows one.
struct ExternalMemory : Memory {
    virtual Result<FileDescriptor> exportSyncFile(Access access) const = 0;
    virtual Result<void> importSyncFile(int syncFile, Access access) const = 0;
};
} // namespace lutils::compute
