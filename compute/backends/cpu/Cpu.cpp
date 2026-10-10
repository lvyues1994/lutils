#include <algorithm>
#include <limits>
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
namespace {
struct Identity {};
struct CpuBufferImpl final : Buffer {
    CpuBufferImpl(std::shared_ptr<Identity> device, std::size_t count)
        : identity(std::move(device)), words(count) {}
    std::size_t wordCount() const noexcept override { return words.size(); }
    std::size_t byteCount() const noexcept override {
        return logicalBytes ? logicalBytes : words.size() * 4;
    }
    std::optional<ImageDesc> imageDescription() const override { return image; }
    std::shared_ptr<Identity> identity;
    std::vector<Word> words;
    std::optional<ImageDesc> image;
    std::size_t logicalBytes = 0;
};
struct CpuKernelImpl final : Kernel {
    CpuKernelImpl(std::shared_ptr<Identity> device, KernelSource source)
        : identity(std::move(device)), description(std::move(source)) {}
    KernelSource const &source() const noexcept override { return description; }
    std::shared_ptr<Identity> identity;
    KernelSource description;
};
struct CpuCompletion final : Completion {
    std::vector<std::pair<ReadbackToken, WordSnapshot>> results;
    Result<void> wait() override { return {}; }
    Result<bool> ready() override { return true; }
    Result<WordSnapshot> readback(ReadbackToken const &token) override {
        for (auto const &result : results)
            if (result.first == token)
                return result.second;
        return Error{ErrorCode::InvalidArgument,
                     "readback token does not belong to this submission"};
    }
};
struct CpuDeviceImpl final : Device {
    std::shared_ptr<Identity> identity = std::make_shared<Identity>();
    std::unique_ptr<CpuExecution> execution;
    explicit CpuDeviceImpl(std::unique_ptr<CpuExecution> e) : execution(std::move(e)) {}
    DeviceInfo info() const override {
        ComputeCapabilities capabilities{true, true, true, 1024, 65536};
        capabilities.float64 = true;
        return {"C++17 CPU", true, std::numeric_limits<std::size_t>::max(), capabilities};
    }
    Result<BufferHandle> createBuffer(std::size_t words) override {
        if (words > std::numeric_limits<Word>::max())
            return Error{ErrorCode::Overflow, "buffer exceeds 32-bit kernel indexing"};
        return BufferHandle{std::make_shared<CpuBufferImpl>(identity, words)};
    }
    Result<BufferHandle> createBufferBytes(std::size_t bytes) override {
        if (bytes > std::size_t{UINT32_MAX} * 4)
            return Error{ErrorCode::Overflow, "buffer exceeds 32-bit kernel indexing"};
        auto result = std::make_shared<CpuBufferImpl>(identity, (bytes + 3) / 4);
        result->logicalBytes = bytes;
        return BufferHandle{std::move(result)};
    }
    Result<KernelHandle> createKernel(KernelSource source) override {
        auto status = validate(source);
        if (!status)
            return status.error();
        if (!source.cpu && !source.cpuDispatch)
            return Error{ErrorCode::Unsupported, "kernel has no CPU entry"};
        if (source.sharedMemoryBytes > 65536 || source.localSize.x > 1024 ||
            source.localSize.y > 1024 || source.localSize.z > 1024 ||
            std::uint64_t{source.localSize.x} * source.localSize.y * source.localSize.z > 1024)
            return Error{ErrorCode::Unsupported, "kernel exceeds CPU workgroup limits"};
        return KernelHandle{std::make_shared<CpuKernelImpl>(identity, std::move(source))};
    }
    Result<BufferHandle> createImage(ImageDesc const &desc) override {
        auto count = imageWordCount(desc);
        if (!count)
            return count.error();
        auto result = std::make_shared<CpuBufferImpl>(identity, count.value());
        result->image = desc;
        return BufferHandle{std::move(result)};
    }
    CpuBufferImpl *get(BufferHandle const &buffer) const {
        auto *result = dynamic_cast<CpuBufferImpl *>(buffer.get());
        return result && result->identity == identity ? result : nullptr;
    }
    Result<void> upload(BufferHandle const &buffer, std::vector<Word> const &words) override {
        CommandList commands;
        auto recorded = commands.upload(buffer, words);
        if (!recorded)
            return recorded;
        auto done = submit(commands);
        return done ? done.value()->wait() : Result<void>{done.error()};
    }
    Result<std::vector<Word>> download(BufferHandle const &buffer) override {
        CommandList commands;
        auto token = commands.readback(buffer);
        if (!token)
            return token.error();
        auto done = submit(commands);
        if (!done)
            return done.error();
        auto data = done.value()->readback(token.value());
        if (!data)
            return data.error();
        return *data.value();
    }
    bool belongs(Upload const &command) const { return get(command.buffer) != nullptr; }
    bool belongs(Readback const &command) const { return get(command.buffer) != nullptr; }
    bool belongs(Dispatch const &d) const {
        auto *k = dynamic_cast<CpuKernelImpl *>(d.kernel.get());
        return k && k->identity == identity &&
               std::all_of(d.buffers.begin(), d.buffers.end(),
                           [&](auto const &buffer) { return get(buffer) != nullptr; });
    }
    void execute(Upload const &command, CpuCompletion &) {
        auto &words = get(command.buffer)->words;
        std::copy(command.words->begin(), command.words->end(), words.begin());
        if (auto image = command.buffer->imageDescription()) {
            auto bytes = kernel::formatInfo(image->format).bytes * image->extent.x *
                         image->extent.y * image->extent.z;
            if (bytes % sizeof(Word)) {
                auto *tail = reinterpret_cast<unsigned char *>(words.data()) + bytes;
                std::fill(tail, tail + (sizeof(Word) - bytes % sizeof(Word)),
                          static_cast<unsigned char>(0));
            }
        }
    }
    void execute(Readback const &command, CpuCompletion &result) {
        result.results.emplace_back(
            command.token, std::make_shared<std::vector<Word> const>(get(command.buffer)->words));
    }
    void execute(Dispatch const &d, CpuCompletion &) {
        std::vector<CpuBuffer> buffers;
        for (auto const &b : d.buffers) {
            auto &words = get(b)->words;
            buffers.push_back({words.data(), words.size(), b->imageDescription(), b->byteCount()});
        }
        if (d.kernel->source().cpuDispatch) {
            d.kernel->source().cpuDispatch(d.extent, buffers, d.parameters, *execution);
            return;
        }
        for (Word z = 0; z < d.extent.z; ++z)
            for (Word y = 0; y < d.extent.y; ++y)
                for (Word x = 0; x < d.extent.x; ++x)
                    d.kernel->source().cpu({x, y, z}, buffers, d.parameters);
    }
    Result<std::shared_ptr<Completion>> submit(CommandList const &commands) override {
        // Validate the entire submission before making any output visible.
        for (auto const &command : commands.commands())
            if (!std::visit([&](auto const &c) { return belongs(c); }, command))
                return Error{ErrorCode::InvalidArgument, "foreign kernel or buffer"};
        auto completion = std::make_shared<CpuCompletion>();
        try {
            for (auto const &command : commands.commands())
                std::visit([&](auto const &c) { execute(c, *completion); }, command);
        } catch (std::out_of_range const &error) {
            return Error{ErrorCode::Bounds, error.what()};
        } catch (std::exception const &error) {
            return Error{ErrorCode::Device, error.what()};
        } catch (...) {
            return Error{ErrorCode::Device, "CPU kernel raised an unknown exception"};
        }
        return std::shared_ptr<Completion>{std::move(completion)};
    }
};
} // namespace
Result<std::unique_ptr<Device>> createCpuDevice(CpuOptions options) {
    auto execution = createCpuExecution(options);
    if (!execution)
        return execution.error();
    return std::unique_ptr<Device>{std::make_unique<CpuDeviceImpl>(std::move(execution).value())};
}
} // namespace lutils::compute
