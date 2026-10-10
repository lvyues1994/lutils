#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <lutils/compute/FileDescriptor.hpp>
#include <unistd.h>
#include <utility>

namespace lutils::compute {
FileDescriptor::~FileDescriptor() {
    if (fd_ >= 0)
        close(fd_);
}
FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : fd_(other.release()) {}
FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            close(fd_);
        fd_ = other.release();
    }
    return *this;
}
int FileDescriptor::release() noexcept { return std::exchange(fd_, -1); }
Result<FileDescriptor> FileDescriptor::duplicate(int fd) {
    if (fd < 0)
        return Error{ErrorCode::InvalidArgument, "invalid file descriptor"};
    int copy;
    do {
        copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    } while (copy < 0 && errno == EINTR);
    if (copy < 0)
        return Error{ErrorCode::Device,
                     std::string{"duplicate descriptor: "} + std::strerror(errno)};
    return FileDescriptor{copy};
}
} // namespace lutils::compute
