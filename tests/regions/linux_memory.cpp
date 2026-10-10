#include "Fixture.hpp"
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/image/LinuxImage.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace regions_test;
namespace {
bool simulate = false, interruptStart = false, againEnd = false, failEnd = false;
unsigned starts = 0, ends = 0, exports = 0, imports = 0;
} // namespace
extern "C" int __real_ioctl(int fd, unsigned long request, ...);
// Fault-injection seam, not a real dma-buf or hardware interop test.
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    auto *value = va_arg(args, void *);
    va_end(args);
    if (!simulate)
        return __real_ioctl(fd, request, value);
    if (request == DMA_BUF_IOCTL_EXPORT_SYNC_FILE) {
        ++exports;
        static_cast<dma_buf_export_sync_file *>(value)->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        return 0;
    }
    if (request == DMA_BUF_IOCTL_IMPORT_SYNC_FILE) {
        ++imports;
        return 0;
    }
    if (request == DMA_BUF_IOCTL_SYNC) {
        if (static_cast<dma_buf_sync *>(value)->flags & DMA_BUF_SYNC_END) {
            if (againEnd) {
                againEnd = false;
                errno = EAGAIN;
                return -1;
            }
            if (failEnd) {
                failEnd = false;
                errno = EIO;
                return -1;
            }
            ++ends;
        } else {
            if (interruptStart) {
                interruptStart = false;
                errno = EINTR;
                return -1;
            }
            ++starts;
        }
        return 0;
    }
    errno = ENOTTY;
    return -1;
}
int main() {
    check(!co::DmaBufMemory::import(-1));
    char path[] = "/tmp/lutils-regions-memory-XXXXXX";
    co::FileDescriptor fd{mkstemp(path)};
    check(fd.get() >= 0);
    unlink(path);
    check(ftruncate(fd.get(), 4096) == 0);
    check(!co::DmaBufMemory::import(fd.get())); // A real regular fd is rejected.
    simulate = true;
    auto memory = take(co::DmaBufMemory::import(fd.get()));
    check(memory->byteCount() == 4096 && memory->fd() != fd.get());
    check((fcntl(memory->fd(), F_GETFD) & FD_CLOEXEC) != 0);
    auto alias = take(co::DmaBufMemory::import(fd.get()));
    check(memory->aliases(*alias));
    interruptStart = true;
    againEnd = true;
    auto mapping = take(memory->map(co::Access::ReadWrite));
    auto *data = take(mapping->writableData());
    data[2] = std::byte{91};
    ok(mapping->finish());
    ok(mapping->finish());
    check(starts == 1 && ends == 1);
    check(!mapping->writableData());
    mapping.reset();
    {
        auto read = take(memory->map(co::Access::Read));
        check(read->data()[2] == std::byte{91} && !read->writableData());
    }
    check(starts == 2 && ends == 2);
    auto failed = take(memory->map(co::Access::Read));
    failEnd = true;
    check(!failed->finish());
    failed.reset(); // retry cleanup after a reported END error
    check(starts == 3 && ends == 3);
    auto fence = take(memory->exportSyncFile(co::Access::ReadWrite));
    ok(memory->importSyncFile(fence.get(), co::Access::Write));
    check(exports >= 3 && imports == 1);
    check(!memory->importSyncFile(-1, co::Access::Read));
    auto spec = formats()[3];
    im::DmaBufImageDesc desc{{4, 4, spec.format, {}, im::Scan::Progressive},
                             {fd.get(), fd.get()},
                             {{0, 0, 4}, {1, 16, 4}},
                             0};
    auto image = take(im::importDmaBufImage(desc));
    check(image.objects().size() == 1 && image.planes()[1].object == 0);
    desc.modifier.reset();
    check(!im::importDmaBufImage(desc));
    desc.modifier = 1;
    check(!im::importDmaBufImage(desc));
    auto descriptor = memory->fd();
    memory.reset();
    check(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    check(fcntl(fd.get(), F_GETFD) >= 0); // caller's descriptor was never consumed
    std::byte const constant[]{std::byte{7}};
    auto host = take(co::borrowMemory(constant, 1));
    check(!host->map(co::Access::Write));
    auto read = take(host->map(co::Access::Read));
    check(!read->writableData() && read->data()[0] == std::byte{7});
    std::cout << "Linux memory: descriptor ownership, simulated sync retries and cleanup passed\n";
}
