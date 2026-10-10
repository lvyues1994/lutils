#pragma once
#include <lutils/compute/Memory.hpp>

namespace lutils::compute {
// Linux owning descriptor. Construction adopts; duplicate() borrows its argument.
struct FileDescriptor {
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
    ~FileDescriptor();
    FileDescriptor(FileDescriptor const &) = delete;
    FileDescriptor &operator=(FileDescriptor const &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept;
    FileDescriptor &operator=(FileDescriptor &&other) noexcept;
    static Result<FileDescriptor> duplicate(int fd);
    int get() const noexcept { return fd_; }
    int release() noexcept;

  private:
    int fd_;
};
struct DmaBufMemory : Memory {
    // Borrows fd, owns a CLOEXEC duplicate. Queries the actual allocation size.
    static Result<std::shared_ptr<DmaBufMemory>> import(int fd);
    virtual int fd() const noexcept = 0;
    virtual Result<void> wait(Access access) const = 0;
    virtual Result<FileDescriptor> exportSyncFile(Access access) const = 0;
    virtual Result<void> importSyncFile(int syncFile, Access access) const = 0;
};
} // namespace lutils::compute
