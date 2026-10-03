#pragma once
#include <cstdint>
#include <lutils/Result.hpp>
#include <lutils/compute/Kernel.hpp>
#include <memory>
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
enum class Access { Read, Write };
struct CpuBuffer {
    Word *data;
    std::size_t size;
};
using CpuEntry = void (*)(kernel::Invocation, std::vector<CpuBuffer> const &,
                          std::vector<Word> const &);
struct KernelSource {
    std::string name;
    std::vector<Access> bindings;
    std::size_t parameterWords = 0;
    Extent3 localSize;
    CpuEntry cpu = nullptr;
    std::vector<Word> spirv;
    Word abiVersion = 1;
};
struct DeviceInfo {
    std::string name;
    bool software = false;
    std::size_t maxBufferBytes = 0;
};
struct Buffer {
    virtual ~Buffer() = default;
    virtual std::size_t wordCount() const noexcept = 0;
};
struct Kernel {
    virtual ~Kernel() = default;
    virtual KernelSource const &source() const noexcept = 0;
};
using BufferHandle = std::shared_ptr<Buffer>;
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
    virtual Result<KernelHandle> createKernel(KernelSource source) = 0;
    virtual Result<void> upload(BufferHandle const &buffer, std::vector<Word> const &words) = 0;
    virtual Result<std::vector<Word>> download(BufferHandle const &buffer) = 0;
    // Submission preserves command order. CPU executes synchronously; Vulkan may
    // wait for an older submission when its configured in-flight limit is reached.
    virtual Result<std::shared_ptr<Completion>> submit(CommandList const &commands) = 0;
};
Result<void> validate(KernelSource const &source);
Result<std::unique_ptr<Device>> createCpuDevice();
// Defined by the optional lutils::compute_vulkan target.
Result<std::unique_ptr<Device>> createVulkanDevice();
} // namespace lutils::compute
