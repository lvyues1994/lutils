#pragma once
#include <cstdint>
#include <functional>
#include <lutils/Result.hpp>
#include <lutils/compute/Kernel.hpp>
#include <lutils/compute/Pixel.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lutils::compute {
using Word = std::uint32_t;
struct Extent3 {
    Word x = 1;
    Word y = 1;
    Word z = 1;
};
enum class Access { Read, Write, ReadWrite };
struct ImageDesc {
    kernel::ImageFormat format = kernel::ImageFormat::R8UI;
    std::uint32_t dimensions = 2;
    Extent3 extent;
};
inline bool operator==(ImageDesc const &a, ImageDesc const &b) {
    return a.format == b.format && a.dimensions == b.dimensions && a.extent.x == b.extent.x &&
           a.extent.y == b.extent.y && a.extent.z == b.extent.z;
}
struct BindingInfo {
    std::uint32_t slot = 0;
    std::size_t elementWords = 1;
    std::optional<ImageDesc> image;
    // Zero retains the ABI v1/v2 word stride; generated typed shaders set byte stride.
    std::size_t elementBytes = 0;
};
Result<std::size_t> imageWordCount(ImageDesc const &desc);
struct CpuBuffer {
    Word *data;
    std::size_t size;
    std::optional<ImageDesc> image = {};
    std::size_t logicalBytes = 0;
    std::size_t byteCount() const { return logicalBytes ? logicalBytes : size * sizeof(Word); }
};
using CpuEntry = void (*)(kernel::Invocation, std::vector<CpuBuffer> const &,
                          std::vector<Word> const &);
struct CpuExecution {
    virtual ~CpuExecution() = default;
    // Calls finish before returning; exceptions are rethrown after joining all work.
    virtual void parallelFor(std::size_t count,
                             std::function<void(std::size_t, std::size_t)> const &) = 0;
    virtual void workgroup(std::size_t lanes, std::function<void(std::size_t)> const &) = 0;
};
using CpuDispatch = void (*)(Extent3, std::vector<CpuBuffer> const &, std::vector<Word> const &,
                             CpuExecution &);
struct KernelSource {
    std::string name;
    std::vector<Access> bindings;
    std::size_t parameterWords = 0;
    Extent3 localSize;
    CpuEntry cpu = nullptr;
    std::vector<Word> spirv;
    Word abiVersion = 1;
    std::vector<BindingInfo> resources;
    CpuDispatch cpuDispatch = nullptr;
    std::size_t sharedMemoryBytes = 0;
    bool requiresFullWorkgroups = false;
};
struct ComputeCapabilities {
    bool float16 = false;
    bool storageBuffer16 = false;
    bool pushConstant16 = false;
    std::size_t maxWorkgroupInvocations = 1;
    std::size_t maxSharedMemoryBytes = 0;
    bool externalDmaBuf = false;
    bool externalAndroidHardwareBuffer = false;
    bool float64 = false;
};
struct DeviceInfo {
    std::string name;
    bool software = false;
    std::size_t maxBufferBytes = 0;
    ComputeCapabilities capabilities{};
};
struct Buffer {
    virtual ~Buffer() = default;
    virtual std::size_t wordCount() const noexcept = 0;
    virtual std::size_t byteCount() const noexcept { return wordCount() * sizeof(Word); }
    virtual std::optional<ImageDesc> imageDescription() const { return {}; }
    virtual bool aliases(Buffer const &other) const noexcept { return this == &other; }
    virtual bool writable() const noexcept { return true; }
};
struct Kernel {
    virtual ~Kernel() = default;
    virtual KernelSource const &source() const noexcept = 0;
};
using BufferHandle = std::shared_ptr<Buffer>;
struct Memory;
using MemoryHandle = std::shared_ptr<Memory>;
using KernelHandle = std::shared_ptr<Kernel>;
struct Dispatch {
    KernelHandle kernel;
    std::vector<BufferHandle> buffers;
    std::vector<Word> parameters;
    Extent3 extent;
};
using WordSnapshot = std::shared_ptr<std::vector<Word> const>;
struct ReadbackToken {
    ReadbackToken() = default;
    friend bool operator==(ReadbackToken const &a, ReadbackToken const &b) noexcept {
        return a.identity_ == b.identity_;
    }

  private:
    struct Identity {};
    std::shared_ptr<Identity const> identity_;
    friend struct CommandList;
};
struct Upload {
    BufferHandle buffer;
    WordSnapshot words;
};
struct Readback {
    BufferHandle buffer;
    ReadbackToken token;
};
using Command = std::variant<Upload, Dispatch, Readback>;
struct CommandList {
    // Copies lvalues; takes ownership of rvalues. After moving, callers must not
    // modify the transferred storage through previously obtained pointers/references.
    Result<void> upload(BufferHandle buffer, std::vector<Word> words);
    Result<void> dispatch(Dispatch dispatch);
    Result<ReadbackToken> readback(BufferHandle buffer);
    std::vector<Command> const &commands() const noexcept { return commands_; }

  private:
    std::vector<Command> commands_;
};
struct Completion {
    // Serialize calls with other completions and operations on the originating Device.
    // ready() may copy completed readbacks and recycle native resources.
    virtual ~Completion() = default;
    virtual Result<void> wait() = 0;
    virtual Result<bool> ready() = 0;
    // Waits for this submission. Returned immutable data outlives the completion
    // handle.
    virtual Result<WordSnapshot> readback(ReadbackToken const &) {
        return Error{ErrorCode::Unsupported, "completion does not support readback"};
    }
};
struct Device {
    // Operations and associated Completion calls require caller serialization.
    virtual ~Device() = default;
    virtual DeviceInfo info() const = 0;
    virtual Result<BufferHandle> createBuffer(std::size_t words) = 0;
    // Direct binding only. Unsupported memory is never uploaded or staged.
    virtual Result<BufferHandle> importMemory(MemoryHandle const &memory);
    virtual Result<BufferHandle> createBufferBytes(std::size_t bytes) {
        if (bytes % sizeof(Word))
            return Error{ErrorCode::Unsupported, "device requires whole-word buffer storage"};
        return createBuffer(bytes / sizeof(Word));
    }
    virtual Result<BufferHandle> createImage(ImageDesc const &) {
        return Error{ErrorCode::Unsupported, "device does not support images"};
    }
    virtual Result<KernelHandle> createKernel(KernelSource source) = 0;
    virtual Result<void> upload(BufferHandle const &buffer, std::vector<Word> const &words) = 0;
    virtual Result<std::vector<Word>> download(BufferHandle const &buffer) = 0;
    // Submission preserves command order. CPU executes synchronously; Vulkan may
    // wait for an older submission when its configured in-flight limit is reached.
    virtual Result<std::shared_ptr<Completion>> submit(CommandList const &commands) = 0;
};
Result<void> validate(KernelSource const &source);
struct CpuOptions {
    // Zero selects up to eight hardware threads; one selects the serial reference path.
    std::size_t workerCount = 0;
    std::size_t parallelThreshold = 4096;
};
Result<std::unique_ptr<CpuExecution>> createCpuExecution(CpuOptions options = {});
Result<std::unique_ptr<Device>> createCpuDevice(CpuOptions options = {});
// Defined by the optional lutils::compute_vulkan target.
Result<std::unique_ptr<Device>> createVulkanDevice();
} // namespace lutils::compute
