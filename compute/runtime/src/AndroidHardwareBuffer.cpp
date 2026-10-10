#include <android/hardware_buffer.h>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <linux/sync_file.h>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <mutex>
#include <poll.h>
#include <sys/ioctl.h>
#include <unordered_map>

namespace lutils::compute {
namespace {
using BufferId = int (*)(AHardwareBuffer const *, std::uint64_t *);
BufferId bufferIdFunction() noexcept {
    // Keep API 26 builds usable. Reliable identity for interop requires Android 12 (API 31).
    static auto const function =
        reinterpret_cast<BufferId>(dlsym(RTLD_DEFAULT, "AHardwareBuffer_getId"));
    return function;
}
Error failure(char const *operation, int code) {
    return {ErrorCode::Device, std::string{operation} + " failed (" + std::to_string(code) + ")"};
}
template <class T> int ioctlRetry(int fd, unsigned int request, T *value) {
    int result;
    do {
        result = ioctl(fd, request, value);
    } while (result < 0 && errno == EINTR);
    return result;
}
Result<void> validateFence(int fd) {
    sync_file_info info{};
    if (ioctlRetry(fd, SYNC_IOC_FILE_INFO, &info) < 0)
        return Error{ErrorCode::InvalidArgument, "AHardwareBuffer dependency is not a sync-file"};
    if (info.status < 0)
        return failure("AHardwareBuffer producer fence", info.status);
    return {};
}
struct BufferState {
    std::shared_ptr<AHardwareBuffer> buffer;
    AHardwareBuffer_Desc description{};
    std::uint64_t id;
    FileDescriptor fence;
    bool locked = false;
    BufferState(std::shared_ptr<AHardwareBuffer> b, AHardwareBuffer_Desc d, std::uint64_t identity)
        : buffer(std::move(b)), description(d), id(identity) {}
    Result<void> wait() const {
        if (fence.get() < 0)
            return {};
        pollfd p{fence.get(), POLLIN, 0};
        int result;
        do {
            result = poll(&p, 1, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return failure("wait AHardwareBuffer fence", result < 0 ? errno : p.revents);
        return validateFence(fence.get());
    }
};
struct AndroidMapping final : MemoryMapping {
    std::shared_ptr<BufferState> state;
    void *address = nullptr;
    bool write;
    bool active = false;
    AndroidMapping(std::shared_ptr<BufferState> s, bool w) : state(std::move(s)), write(w) {}
    ~AndroidMapping() override {
        if (active)
            (void)finish();
    }
    std::byte const *data() const noexcept override {
        return active ? static_cast<std::byte const *>(address) : nullptr;
    }
    Result<std::byte *> writableData() override {
        if (!active || !write)
            return Error{ErrorCode::InvalidArgument,
                         "AHardwareBuffer mapping is read-only or finished"};
        return static_cast<std::byte *>(address);
    }
    Result<void> finish() override {
        if (!active)
            return {};
        auto const result = AHardwareBuffer_unlock(state->buffer.get(), nullptr);
        if (result)
            return failure("AHardwareBuffer_unlock", result);
        active = false;
        state->locked = false;
        state->fence = FileDescriptor{}; // unlock(nullptr) completes synchronously
        address = nullptr;
        return {};
    }
};
struct AndroidMemory final : AndroidHardwareBufferMemory {
    std::shared_ptr<BufferState> state;
    explicit AndroidMemory(std::shared_ptr<BufferState> s) : state(std::move(s)) {}
    AHardwareBuffer *nativeBuffer() const noexcept override { return state->buffer.get(); }
    std::size_t byteCount() const noexcept override { return state->description.width; }
    bool writable() const noexcept override { return true; }
    bool aliases(Memory const &other) const noexcept override {
        auto const *b = dynamic_cast<AndroidMemory const *>(&other);
        return b && state->id == b->state->id;
    }
    Result<FileDescriptor> exportSyncFile(Access) const override {
        if (state->locked)
            return Error{ErrorCode::InvalidArgument, "AHardwareBuffer is CPU mapped"};
        if (state->fence.get() < 0)
            return FileDescriptor{};
        auto valid = validateFence(state->fence.get());
        if (!valid)
            return valid.error();
        return FileDescriptor::duplicate(state->fence.get());
    }
    Result<void> importSyncFile(int fd, Access) const override {
        if (state->locked || fd < -1)
            return Error{ErrorCode::InvalidArgument, "invalid fence or active CPU mapping"};
        if (fd == -1)
            return {};
        auto valid = validateFence(fd);
        if (!valid)
            return valid.error();
        if (state->fence.get() >= 0) {
            sync_merge_data merge{};
            std::memcpy(merge.name, "lutils AHB", sizeof("lutils AHB"));
            merge.fd2 = fd;
            if (ioctlRetry(state->fence.get(), SYNC_IOC_MERGE, &merge) < 0)
                return failure("merge AHardwareBuffer fences", errno);
            state->fence = FileDescriptor{merge.fence};
        } else {
            auto copy = FileDescriptor::duplicate(fd);
            if (!copy)
                return copy.error();
            state->fence = std::move(copy).value();
        }
        return {};
    }
    Result<std::unique_ptr<MemoryMapping>> map(Access access) override {
        if (state->locked)
            return Error{ErrorCode::InvalidArgument,
                         "AHardwareBuffer already has an active mapping"};
        auto const usage = state->description.usage;
        std::uint64_t requested = 0;
        if (access != Access::Write)
            requested |= usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK;
        if (access != Access::Read)
            requested |= usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK;
        if ((access != Access::Write && !(requested & AHARDWAREBUFFER_USAGE_CPU_READ_MASK)) ||
            (access != Access::Read && !(requested & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK)))
            return Error{ErrorCode::Unsupported, "AHardwareBuffer lacks requested CPU usage"};
        auto ready = state->wait();
        if (!ready)
            return ready.error();
        auto mapping = std::make_unique<AndroidMapping>(state, access != Access::Read);
        auto const result =
            AHardwareBuffer_lock(nativeBuffer(), requested, -1, nullptr, &mapping->address);
        if (result)
            return failure("AHardwareBuffer_lock", result);
        mapping->active = true;
        state->locked = true;
        return std::unique_ptr<MemoryMapping>{std::move(mapping)};
    }
};
// All imports of the same AHB share fence state, including handles rebuilt through IPC.
struct Imports {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<BufferState>> buffers;
};
} // namespace
bool AndroidHardwareBufferMemory::available() noexcept { return bufferIdFunction() != nullptr; }
Result<std::shared_ptr<AndroidHardwareBufferMemory>>
AndroidHardwareBufferMemory::import(AHardwareBuffer *buffer) {
    if (!buffer)
        return Error{ErrorCode::InvalidArgument, "null AHardwareBuffer"};
    auto const identify = bufferIdFunction();
    if (!identify)
        return Error{ErrorCode::Unsupported,
                     "AHardwareBuffer interop requires stable IDs (Android API 31)"};
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);
    if (description.format != AHARDWAREBUFFER_FORMAT_BLOB || description.height != 1 ||
        description.layers != 1 || !description.width ||
        !(description.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER) ||
        (description.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT))
        return Error{ErrorCode::Unsupported,
                     "expected an unprotected AHardwareBuffer BLOB with GPU_DATA_BUFFER usage"};
    std::uint64_t id = 0;
    auto const result = identify(buffer, &id);
    if (result)
        return failure("AHardwareBuffer_getId", result);
    static Imports imports;
    auto lock = std::lock_guard<std::mutex>{imports.mutex};
    for (auto it = imports.buffers.begin(); it != imports.buffers.end();) {
        if (it->second.expired())
            it = imports.buffers.erase(it);
        else
            ++it;
    }
    auto found = imports.buffers.find(id);
    if (found != imports.buffers.end())
        if (auto existing = found->second.lock())
            return std::shared_ptr<AndroidHardwareBufferMemory>{
                std::make_shared<AndroidMemory>(std::move(existing))};
    AHardwareBuffer_acquire(buffer);
    auto owner = std::shared_ptr<AHardwareBuffer>{buffer, AHardwareBuffer_release};
    auto memory = std::make_shared<AndroidMemory>(
        std::make_shared<BufferState>(std::move(owner), description, id));
    imports.buffers[id] = memory->state;
    return std::shared_ptr<AndroidHardwareBufferMemory>{std::move(memory)};
}
Result<std::shared_ptr<AndroidHardwareBufferMemory>>
AndroidHardwareBufferMemory::allocate(std::size_t bytes) {
    if (!bytes || bytes > std::numeric_limits<std::uint32_t>::max())
        return Error{ErrorCode::InvalidArgument, "AHardwareBuffer byte capacity is out of range"};
    if (!available())
        return Error{ErrorCode::Unsupported, "AHardwareBuffer interop requires Android API 31"};
    AHardwareBuffer_Desc description{};
    description.width = static_cast<std::uint32_t>(bytes);
    description.height = 1;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_BLOB;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER |
                        AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    AHardwareBuffer *buffer = nullptr;
    auto const result = AHardwareBuffer_allocate(&description, &buffer);
    if (result)
        return failure("AHardwareBuffer_allocate", result);
    auto owner = std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)>{
        buffer, AHardwareBuffer_release};
    return import(owner.get());
}
} // namespace lutils::compute
