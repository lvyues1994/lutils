#include <algorithm>
#include <iostream>
#include <lutils/image/CpuConversionExecutor.hpp>
#include <stdexcept>
#ifdef __ANDROID__
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#endif
namespace im = lutils::image;
namespace co = lutils::compute;
using lutils::Error;
using lutils::ErrorCode;
using lutils::Result;
namespace {
void check(bool b) {
    if (!b)
        throw std::runtime_error("CPU conversion resource contract failed");
}
template <class T> T take(Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
struct State {
    std::vector<std::byte> bytes = std::vector<std::byte>(96, std::byte{128});
    int maps = 0, finishes = 0;
    co::Access access = co::Access::Read;
    bool failMap = false, failWrite = false, failFinish = false;
};
struct Mapping final : co::MemoryMapping {
    std::shared_ptr<State> state;
    bool active = true;
    explicit Mapping(std::shared_ptr<State> s) : state(std::move(s)) {}
    ~Mapping() override {
        if (active)
            (void)finish();
    }
    std::byte const *data() const noexcept override { return state->bytes.data(); }
    Result<std::byte *> writableData() override {
        if (state->failWrite)
            return Error{ErrorCode::Unsupported, "write failure"};
        return state->bytes.data();
    }
    Result<void> finish() override {
        if (!active)
            return {};
        active = false;
        ++state->finishes;
        if (state->failFinish)
            return Error{ErrorCode::Unsupported, "finish failure"};
        return {};
    }
};
struct Memory final : co::Memory {
    std::shared_ptr<State> state = std::make_shared<State>();
    std::size_t byteCount() const noexcept override { return state->bytes.size(); }
    bool writable() const noexcept override { return true; }
    bool aliases(co::Memory const &other) const noexcept override { return this == &other; }
    Result<std::unique_ptr<co::MemoryMapping>> map(co::Access access) override {
        state->access = access;
        ++state->maps;
        if (state->failMap)
            return Error{ErrorCode::Unsupported, "map failure"};
        return std::unique_ptr<co::MemoryMapping>{std::make_unique<Mapping>(state)};
    }
};
struct NativeMapping final : co::NativeImageMapping {
    std::shared_ptr<State> state;
    std::vector<co::NativePlane> view;
    bool active = true;
    ~NativeMapping() override {
        if (active)
            (void)finish();
    }
    std::vector<co::NativePlane> const &planes() const noexcept override { return view; }
    Result<std::byte *> writablePlane(std::size_t p) override {
        if (p != 0 || state->access != co::Access::Write)
            return Error{ErrorCode::Bounds, "plane failure"};
        return state->bytes.data();
    }
    Result<void> finish() override {
        if (active)
            ++state->finishes;
        active = false;
        view.clear();
        return {};
    }
};
struct Native final : co::NativeImage {
    std::shared_ptr<State> state = std::make_shared<State>();
    std::size_t pixelStride = 4, capacity = 96;
    co::NativeImageDesc description() const noexcept override {
        return {4, 4, co::NativeImageFormat::Rgba8};
    }
    bool writable() const noexcept override { return true; }
    bool aliases(co::NativeImage const &other) const noexcept override { return this == &other; }
    Result<std::unique_ptr<co::NativeImageMapping>> map(co::Access access) override {
        check(access == co::Access::Write);
        state->access = access;
        ++state->maps;
        auto m = std::make_unique<NativeMapping>();
        m->state = state;
        m->view = {{state->bytes.data(), 24, pixelStride, capacity}};
        return std::unique_ptr<co::NativeImageMapping>{std::move(m)};
    }
    Result<co::FileDescriptor> exportSyncFile(co::Access) const override {
        return co::FileDescriptor{};
    }
    Result<void> importSyncFile(int, co::Access) const override { return {}; }
};
void run() {
    im::FrameDesc desc{2,
                       2,
                       im::nv12(),
                       {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                        im::ChromaLocation::Midpoint}};
    auto out = desc;
    out.format = im::rgba8();
    out.color = {im::Matrix::Identity, im::Range::Full};
    auto plan = take(im::CpuConversionPlan::prepare(desc, out));
    auto source = std::make_shared<Memory>(), dest = std::make_shared<Memory>();
    auto input = take(im::ImageResource::linear(desc, {source}, {{0, 0, 8}, {0, 16, 8}}));
    auto output = take(im::ImageResource::linear(out, {dest}, {{0, 0, 16}}));
    auto executor = im::createCpuConversionExecutor();
#ifdef __ANDROID__
    // Owned BLOB allocations exercise the real lock/unlock path without external devices.
    auto blobIn = take(co::AndroidHardwareBufferMemory::allocate(96));
    auto blobOut = take(co::AndroidHardwareBufferMemory::allocate(96));
    {
        auto mapping = take(blobIn->map(co::Access::Write));
        std::fill_n(take(mapping->writableData()), 96, std::byte{128});
        take(mapping->finish());
    }
    auto ahbIn = take(im::ImageResource::linear(desc, {blobIn}, {{0, 0, 8}, {0, 16, 8}}));
    auto ahbOut = take(im::ImageResource::linear(out, {blobOut}, {{0, 0, 16}}));
    take(executor->run(plan, ahbIn, ahbOut));
    {
        auto mapping = take(blobOut->map(co::Access::Read));
        check(mapping->data()[0] == std::byte{130} && mapping->data()[3] == std::byte{255});
        take(mapping->finish());
    }
#endif
    for (int i = 1; i <= 2; ++i) {
        auto done = take(executor->submit(plan, input, output));
        check(take(done->ready()));
        take(done->wait());
        check(source->state->maps == i && source->state->finishes == i);
        check(dest->state->maps == i && dest->state->finishes == i);
        check(source->state->access == co::Access::Read &&
              dest->state->access == co::Access::Write);
        for (std::size_t y = 0; y < 2; ++y)
            for (std::size_t x = 0; x < 8; ++x)
                check(dest->state->bytes[16 * y + x] == std::byte(x % 4 == 3 ? 255 : 130));
    }
    auto alias = take(im::ImageResource::linear(out, {source}, {{0, 0, 16}}));
    check(!executor->run(plan, input, alias));
    check(source->state->maps == 2);
    check(!executor->run(plan, output, input));
    check(source->state->maps == 2);
    for (int fault = 0; fault < 4; ++fault) {
        auto beforeIn = source->state->finishes, beforeOut = dest->state->finishes;
        dest->state->failMap = fault == 0;
        dest->state->failWrite = fault == 1;
        dest->state->failFinish = fault == 2;
        source->state->failFinish = fault == 3;
        check(!executor->run(plan, input, output));
        check(source->state->finishes == beforeIn + 1);
        check(dest->state->finishes == beforeOut + (fault == 0 ? 0 : 1));
    }
    source->state->failFinish = false;
    dest->state->failFinish = false;
    auto native = std::make_shared<Native>();
    auto nativeOut = take(im::ImageResource::native(native, out.color, {1, 1, 2, 2}));
    auto original = native->state->bytes;
    take(executor->run(plan, input, nativeOut));
    for (std::size_t i = 0; i < original.size(); ++i) {
        bool written = (i >= 28 && i < 36) || (i >= 52 && i < 60);
        check(native->state->bytes[i] ==
              (written ? std::byte(i % 4 == 3 ? 255 : 130) : original[i]));
    }
    check(native->state->maps == 1 && native->state->finishes == 1);
    original = native->state->bytes;
    native->pixelStride = 5;
    check(!executor->run(plan, input, nativeOut));
    check(native->state->bytes == original);
    native->pixelStride = 4;
    native->capacity = 1;
    check(!executor->run(plan, input, nativeOut));
    check(native->state->bytes == original);
    check(native->state->maps == 3 && native->state->finishes == 3);
}
} // namespace
int main() {
    try {
        run();
        std::cout << "CPU conversion mappings, synchronization and native RGBA passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
