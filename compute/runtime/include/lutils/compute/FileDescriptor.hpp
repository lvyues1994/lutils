#pragma once
#include <lutils/Result.hpp>

namespace lutils::compute {
// POSIX owning descriptor. Construction adopts; duplicate() borrows its argument.
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
} // namespace lutils::compute
