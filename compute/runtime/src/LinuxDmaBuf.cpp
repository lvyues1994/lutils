#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lutils::compute {
namespace {
Error osError(char const *operation) {
    auto code = errno;
    return {code == ENOTTY || code == ENOSYS || code == ENOTSUP ? ErrorCode::Unsupported
                                                                : ErrorCode::Device,
            std::string{operation} + ": " + std::strerror(code)};
}
template <class T> int ioctlRetry(int fd, unsigned long request, T *value) {
    int result;
    do {
        result = ioctl(fd, request, value);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    return result;
}
std::uint32_t accessFlags(Access access) {
    return access == Access::Read    ? DMA_BUF_SYNC_READ
           : access == Access::Write ? DMA_BUF_SYNC_WRITE
                                     : DMA_BUF_SYNC_RW;
}
struct DmaMapping final : MemoryMapping {
    FileDescriptor descriptor;
    void *base = MAP_FAILED;
    std::size_t bytes;
    std::uint32_t flags;
    bool active = false;
    DmaMapping(FileDescriptor d, std::size_t n, std::uint32_t f)
        : descriptor(std::move(d)), bytes(n), flags(f) {}
    ~DmaMapping() override {
        if (active)
            (void)finish();
        if (base != MAP_FAILED)
            munmap(base, bytes);
    }
    std::byte const *data() const noexcept override { return static_cast<std::byte const *>(base); }
    Result<std::byte *> writableData() override {
        if (!(flags & DMA_BUF_SYNC_WRITE) || !active)
            return Error{ErrorCode::InvalidArgument, "mapping is read-only or finished"};
        return static_cast<std::byte *>(base);
    }
    Result<void> finish() override {
        if (!active)
            return {};
        dma_buf_sync sync{DMA_BUF_SYNC_END | flags};
        if (ioctlRetry(descriptor.get(), DMA_BUF_IOCTL_SYNC, &sync) < 0)
            return osError("DMA_BUF_SYNC_END");
        active = false;
        return {};
    }
};
struct DmaMemory final : DmaBufMemory {
    FileDescriptor descriptor;
    std::size_t bytes;
    dev_t device;
    ino_t inode;
    bool write;
    DmaMemory(FileDescriptor d, std::size_t n, struct stat const &s, bool w)
        : descriptor(std::move(d)), bytes(n), device(s.st_dev), inode(s.st_ino), write(w) {}
    int fd() const noexcept override { return descriptor.get(); }
    std::size_t byteCount() const noexcept override { return bytes; }
    bool writable() const noexcept override { return write; }
    bool aliases(Memory const &other) const noexcept override {
        auto const *d = dynamic_cast<DmaMemory const *>(&other);
        return d && device == d->device && inode == d->inode;
    }
    Result<void> wait(Access access) const override {
        pollfd p{fd(), static_cast<short>(access == Access::Read ? POLLIN : POLLOUT), 0};
        int result;
        do {
            result = poll(&p, 1, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            return osError("poll dma-buf");
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
            return Error{ErrorCode::Device, "dma-buf poll failed"};
        return {};
    }
    Result<FileDescriptor> exportSyncFile(Access access) const override {
        dma_buf_export_sync_file request{accessFlags(access), -1};
        if (ioctlRetry(fd(), DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &request) < 0)
            return osError("DMA_BUF_IOCTL_EXPORT_SYNC_FILE");
        return FileDescriptor{request.fd};
    }
    Result<void> importSyncFile(int syncFile, Access access) const override {
        if (syncFile < 0)
            return Error{ErrorCode::InvalidArgument, "invalid sync-file descriptor"};
        dma_buf_import_sync_file request{accessFlags(access), syncFile};
        if (ioctlRetry(fd(), DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &request) < 0)
            return osError("DMA_BUF_IOCTL_IMPORT_SYNC_FILE");
        return {};
    }
    Result<std::unique_ptr<MemoryMapping>> map(Access access) override {
        if (access != Access::Read && !write)
            return Error{ErrorCode::InvalidArgument, "dma-buf is read-only"};
        auto duplicate = FileDescriptor::duplicate(fd());
        if (!duplicate)
            return duplicate.error();
        auto waited = wait(access);
        if (!waited)
            return waited.error();
        int protection = PROT_READ | (access == Access::Read ? 0 : PROT_WRITE);
        auto mapping =
            std::make_unique<DmaMapping>(std::move(duplicate).value(), bytes, accessFlags(access));
        mapping->base = mmap(nullptr, bytes, protection, MAP_SHARED, fd(), 0);
        if (mapping->base == MAP_FAILED)
            return osError("mmap dma-buf");
        dma_buf_sync sync{DMA_BUF_SYNC_START | accessFlags(access)};
        if (ioctlRetry(fd(), DMA_BUF_IOCTL_SYNC, &sync) < 0)
            return osError("DMA_BUF_SYNC_START");
        mapping->active = true;
        return std::unique_ptr<MemoryMapping>{std::move(mapping)};
    }
};
} // namespace
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
        return osError("duplicate descriptor");
    return FileDescriptor{copy};
}
Result<std::shared_ptr<DmaBufMemory>> DmaBufMemory::import(int fd) {
    auto copy = FileDescriptor::duplicate(fd);
    if (!copy)
        return copy.error();
    struct stat status {};
    if (fstat(copy.value().get(), &status) < 0)
        return osError("stat dma-buf");
    auto flags = fcntl(copy.value().get(), F_GETFL);
    if (flags < 0)
        return osError("dma-buf access mode");
    if ((flags & O_ACCMODE) == O_WRONLY || status.st_size <= 0 ||
        static_cast<std::uintmax_t>(status.st_size) > static_cast<std::uintmax_t>(PTRDIFF_MAX))
        return Error{ErrorCode::Unsupported, "dma-buf needs readable, addressable storage"};
    auto memory = std::make_shared<DmaMemory>(std::move(copy).value(),
                                              static_cast<std::size_t>(status.st_size), status,
                                              (flags & O_ACCMODE) == O_RDWR);
    // Verify that this is a dma-buf, rather than silently accepting regular files.
    auto fence = memory->exportSyncFile(Access::Read);
    if (!fence)
        return fence.error();
    return std::shared_ptr<DmaBufMemory>{std::move(memory)};
}
} // namespace lutils::compute
