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
    std::shared_ptr<Identity> identity;
    std::vector<Word> words;
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
    DeviceInfo info() const override {
        return {"C++17 reference CPU", true, std::numeric_limits<std::size_t>::max()};
    }
    Result<BufferHandle> createBuffer(std::size_t words) override {
        if (words > std::numeric_limits<Word>::max())
            return Error{ErrorCode::Overflow, "buffer exceeds 32-bit kernel indexing"};
        return BufferHandle{std::make_shared<CpuBufferImpl>(identity, words)};
    }
    Result<KernelHandle> createKernel(KernelSource source) override {
        auto status = validate(source);
        if (!status)
            return status.error();
        if (!source.cpu)
            return Error{ErrorCode::Unsupported, "kernel has no CPU entry"};
        return KernelHandle{std::make_shared<CpuKernelImpl>(identity, std::move(source))};
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
    }
    void execute(Readback const &command, CpuCompletion &result) {
        result.results.emplace_back(
            command.token, std::make_shared<std::vector<Word> const>(get(command.buffer)->words));
    }
    void execute(Dispatch const &d, CpuCompletion &) {
        std::vector<CpuBuffer> buffers;
        for (auto const &b : d.buffers) {
            auto &words = get(b)->words;
            buffers.push_back({words.data(), words.size()});
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
        }
        return std::shared_ptr<Completion>{std::move(completion)};
    }
};
} // namespace
Result<std::unique_ptr<Device>> createCpuDevice() {
    return std::unique_ptr<Device>{std::make_unique<CpuDeviceImpl>()};
}
} // namespace lutils::compute
