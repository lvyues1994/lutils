#include "Fixture.hpp"
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <linux/sync_file.h>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <poll.h>
#include <sys/ioctl.h>

namespace {
bool injected = false;
int fenceStatus = 0;
} // namespace
extern "C" int __real_ioctl(int, unsigned int, ...);
extern "C" int __real_poll(pollfd *, nfds_t, int);
extern "C" int __wrap_ioctl(int fd, unsigned int request, ...) {
    va_list args;
    va_start(args, request);
    auto value = va_arg(args, void *);
    va_end(args);
    if (injected && request == SYNC_IOC_FILE_INFO) {
        static_cast<sync_file_info *>(value)->status = fenceStatus;
        return 0;
    }
    return __real_ioctl(fd, request, value);
}
extern "C" int __wrap_poll(pollfd *fds, nfds_t count, int timeout) {
    if (injected && count == 1) {
        fds[0].revents = POLLIN;
        return 1;
    }
    return __real_poll(fds, count, timeout);
}
int main() {
    using namespace regions_test;
    using Ahb = co::AndroidHardwareBufferMemory;
    if (!Ahb::available())
        return 77;
    auto memory = take(Ahb::allocate(4096));
    co::FileDescriptor fd{open("/dev/null", O_RDONLY | O_CLOEXEC)};
    injected = true;
    fenceStatus = -EIO;
    check(!memory->importSyncFile(fd.get(), co::Access::Read));
    fenceStatus = 0;
    ok(memory->importSyncFile(fd.get(), co::Access::Read));
    fenceStatus = -EIO;
    check(!memory->exportSyncFile(co::Access::Read));
    auto mapping = memory->map(co::Access::ReadWrite);
    check(!mapping && mapping.error().code == lutils::ErrorCode::Device);
    fenceStatus = 1;
    auto valid = take(memory->map(co::Access::ReadWrite));
    ok(valid->finish());
    injected = false;
    std::cout << "AHB failed-fence propagation passed (ioctl/poll fault injection)\n";
}
