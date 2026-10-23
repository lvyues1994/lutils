#include "Fixture.hpp"
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/image/LinuxImage.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace regions_test;
namespace {
bool simulate = false, interruptStart = false, againEnd = false, failEnd = false;
unsigned starts = 0, ends = 0, exports = 0, imports = 0;
int failEndAfter = -1;
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
            if (failEndAfter == 0) { failEndAfter = -1; errno = EIO; return -1; }
            if (failEndAfter > 0) --failEndAfter;
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
    if (request == SYNC_IOC_MERGE) {
        static_cast<sync_merge_data *>(value)->fence = fcntl(fd, F_DUPFD_CLOEXEC, 0);
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
    co::DmaBufNativeImageDesc native{{4, 4, co::NativeImageFormat::Yuv420},
                                     0x3231564e,
                                     {{fd.get(), 0, 4, 0}, {fd.get(), 16, 4, 0}},
                                     false};
    auto nativeImage = take(co::DmaBufNativeImage::import(native));
    auto overlapping = native;
    overlapping.planes[1].offset = 0;
    check(!co::DmaBufNativeImage::import(overlapping));
    overlapping = native;
    overlapping.planes[0].modifier = 0x00ffffffffffffffULL;
    check(!co::DmaBufNativeImage::import(overlapping));
    auto nativeAlias = take(co::DmaBufNativeImage::import(native));
    check(nativeImage->aliases(*nativeAlias));
    check(nativeImage->dmaDescription().planes[0].fd != fd.get());
    auto nativeMap = take(nativeImage->map(co::Access::ReadWrite));
    check(nativeMap->planes().size() == 3);
    check(nativeMap->planes()[1].pixelStride == 2 && nativeMap->planes()[1].rowStride == 4);
    check(nativeMap->planes()[2].data == nativeMap->planes()[1].data + 1);
    take(nativeMap->writablePlane(1))[0] = std::byte{71};
    ok(nativeMap->finish());
    check(nativeMap->planes().empty() && !nativeMap->writablePlane(1));
    auto nativeRead = take(nativeAlias->map(co::Access::Read));
    check(nativeRead->planes()[1].data[0] == std::byte{71});
    ok(nativeRead->finish());
    auto nativeFence = take(nativeImage->exportSyncFile(co::Access::Read));
    ok(nativeImage->importSyncFile(nativeFence.get(), co::Access::Write));
    native.readOnly = true;
    check(!take(co::DmaBufNativeImage::import(native))->map(co::Access::Write));
    native.planes[0].modifier = native.planes[1].modifier = 1;
    check(!take(co::DmaBufNativeImage::import(native))->map(co::Access::Read));
    native.planes[0].modifier = native.planes[1].modifier = 0;
    native.planes[1].offset = 4095;
    check(!co::DmaBufNativeImage::import(native));
    native.planes[1].offset = 16;
    native.planes[1].stride = 1;
    check(!co::DmaBufNativeImage::import(native));
    native.planes[1].stride = 4;
    native.image.format = co::NativeImageFormat::Rgba8;
    check(!co::DmaBufNativeImage::import(native));
    native.fourcc = 0;
    check(!co::DmaBufNativeImage::import(native));
    co::DmaBufNativeImageDesc rgba{{4, 4, co::NativeImageFormat::Rgba8}, 0x34324241,
                                  {{fd.get(), 0, 16, 0}}, false};
    auto nativeRgba = take(im::importNativeDmaBufImage(rgba));
    auto linearRgba = take(im::ImageResource::linear(nativeRgba.description(), {alias}, {{0, 0, 16}}));
    check(nativeRgba.aliases(linearRgba) && linearRgba.aliases(nativeRgba));
    auto sameCrop = take(im::RegionPlan::crop(nativeRgba.description(), {0, 0, 4, 4}));
    auto cpu = im::createCpuRegionExecutor();
    check(!cpu->submit(sameCrop, nativeRgba, linearRgba));
    check(!cpu->submit(sameCrop, linearRgba, nativeRgba));
    char secondPath[] = "/tmp/lutils-native-memory-XXXXXX";
    co::FileDescriptor secondFd{mkstemp(secondPath)};
    check(secondFd.get() >= 0 && unlink(secondPath) == 0 && ftruncate(secondFd.get(), 4096) == 0);
    co::DmaBufNativeImageDesc split{{4, 4, co::NativeImageFormat::Yuv420}, 0x3231564e,
                                   {{fd.get(), 0, 4, 0}, {secondFd.get(), 0, 4, 0}}, false};
    auto splitImage = take(co::DmaBufNativeImage::import(split));
    auto merged = take(splitImage->exportSyncFile(co::Access::ReadWrite));
    ok(splitImage->importSyncFile(merged.get(), co::Access::Write));
    auto splitMap = take(splitImage->map(co::Access::ReadWrite));
    failEndAfter = 1;
    check(!splitMap->finish());
    check(splitMap->planes().empty() && !splitMap->writablePlane(0) && !splitMap->writablePlane(1));
    ok(splitMap->finish());
    auto descriptor = memory->fd();
    memory.reset();
    check(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    check(fcntl(fd.get(), F_GETFD) >= 0); // caller's descriptor was never consumed
    std::byte const constant[]{std::byte{7}};
    auto host = take(co::borrowMemory(constant, 1));
    check(!host->map(co::Access::Write));
    auto read = take(host->map(co::Access::Read));
    check(!read->writableData() && read->data()[0] == std::byte{7});
    std::cout << "Linux memory: descriptor ownership, simulated sync retries and "
                 "cleanup passed\n";
}
