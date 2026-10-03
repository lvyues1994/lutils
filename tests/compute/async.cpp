#include <add.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <lutils/image/Conversion.hpp>
#include <optional>
#include <string>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif

namespace co = lutils::compute;
namespace im = lutils::image;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " #x "\n";                                                 \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
template <class T> T take(lutils::Result<T> result) {
    if (!result) {
        std::cerr << result.error().message << '\n';
        std::abort();
    }
    return std::move(result).value();
}
void ok(lutils::Result<void> result) {
    if (!result) {
        std::cerr << result.error().message << '\n';
        std::abort();
    }
}
void commands(co::Device &device) {
    auto a = take(device.createBuffer(4));
    auto b = take(device.createBuffer(4));
    auto k = take(device.createKernel(lutils::generated::add()));
    CHECK(take(device.download(a)) == std::vector<co::Word>(4, 0));
    co::CommandList list;
    std::vector<co::Word> original{1, 2, 3, 4};
    ok(list.upload(a, original));
    original.assign(4, 99);
    ok(list.dispatch({k, {a, b}, {4, 3}, {4, 1, 1}}));
    auto firstToken = take(list.readback(b));
    ok(list.upload(a, {8, 9, 10, 11}));
    ok(list.dispatch({k, {a, b}, {4, 1}, {4, 1, 1}}));
    auto secondToken = take(list.readback(b));
    auto first = take(device.submit(list));
    auto second = take(device.submit(list));
    list = {};
    CHECK(*take(second->readback(secondToken)) == std::vector<co::Word>({17, 19, 21, 23}));
    CHECK(*take(first->readback(firstToken)) == std::vector<co::Word>({5, 7, 9, 11}));
    CHECK(take(first->readback(firstToken)) != take(second->readback(firstToken)));
    auto snapshot = take(first->readback(secondToken));
    CHECK(!first->readback({}));
    co::CommandList unrelated;
    CHECK(!first->readback(take(unrelated.readback(a))));
    CHECK(!unrelated.upload({}, {}));
    CHECK(!unrelated.upload(a, {1}));
    CHECK(!unrelated.readback({}));

    // An invalid operation at the end rejects the entire command stream.
    auto foreign = take(co::createCpuDevice());
    auto foreignBuffer = take(foreign->createBuffer(4));
    co::CommandList bad;
    ok(bad.upload(a, {55, 55, 55, 55}));
    (void)take(bad.readback(foreignBuffer));
    CHECK(!device.submit(bad));
    CHECK(take(device.download(a)) == std::vector<co::Word>({8, 9, 10, 11}));

    // Backpressure and reverse waits must preserve all distinct frame results.
    std::vector<std::shared_ptr<co::Completion>> pending;
    std::vector<co::ReadbackToken> tokens;
    for (co::Word i = 0; i < 9; ++i) {
        co::CommandList next;
        ok(next.upload(a, std::vector<co::Word>(4, i)));
        ok(next.dispatch({k, {a, b}, {4, 7}, {4, 1, 1}}));
        tokens.push_back(take(next.readback(b)));
        pending.push_back(take(device.submit(next)));
    }
    for (std::size_t i = pending.size(); i-- > 0;)
        CHECK(*take(pending[i]->readback(tokens[i])) ==
              std::vector<co::Word>(4, static_cast<co::Word>(i) * 2 + 7));
    ok(first->wait());
    CHECK(take(first->ready()));
    CHECK(*take(first->readback(firstToken)) == std::vector<co::Word>({5, 7, 9, 11}));
    first.reset();
    CHECK(*snapshot == std::vector<co::Word>({17, 19, 21, 23}));
    auto zero = take(device.createBuffer(0));
    co::CommandList empty;
    ok(empty.upload(zero, {}));
    auto token = take(empty.readback(zero));
    CHECK(take(take(device.submit(empty))->readback(token))->empty());
}
void imageTransfer(co::Device &device) {
    im::FrameDesc desc{6, 3, im::uyvy422(), {}, im::Scan::Progressive};
    std::optional<im::HostFrame> input{take(im::HostFrame::create(desc, 16))};
    auto output = take(im::HostFrame::create(desc, 16));
    auto source = input->view();
    auto target = output.view();
    source.planes[0].row0 = 32;
    source.planes[0].stride = -16;
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 0; x < 12; ++x)
            im::row(source.planes[0], y)[x] = static_cast<std::byte>(y * 12 + x);
    std::fill_n(target.planes[0].base, target.planes[0].capacity, std::byte{0xA5});
    std::optional<im::DeviceFrame> frame{take(im::DeviceFrame::create(device, desc))};
    co::CommandList list;
    ok(im::recordUpload(list, im::readOnly(source), *frame));
    auto readback = take(im::recordReadback(list, *frame));
    input.reset(); // Upload owns its snapshot; no borrowed host pointer remains.
    auto done = take(device.submit(list));
    list = {};
    frame.reset();
    ok(readback.copyTo(*done, target));
    for (std::size_t y = 0; y < 3; ++y) {
        for (std::size_t x = 0; x < 12; ++x)
            CHECK(im::row(target.planes[0], y)[x] == static_cast<std::byte>(y * 12 + x));
        for (std::size_t x = 12; x < 16; ++x)
            CHECK(im::row(target.planes[0], y)[x] == std::byte{0xA5});
    }
}
void deviceLifetime(std::unique_ptr<co::Device> device) {
    auto buffer = take(device->createBuffer(4));
    co::CommandList commands;
    ok(commands.upload(buffer, {10, 20, 30, 40}));
    auto token = take(commands.readback(buffer));
    auto done = take(device->submit(commands));
    commands = {};
    buffer.reset();
    device.reset();
    CHECK(take(done->ready()));
    CHECK(*take(done->readback(token)) == std::vector<co::Word>({10, 20, 30, 40}));
}
#ifdef LUTILS_TEST_VULKAN
void reuse(co::VulkanOptions options) {
    options.maxInFlight = 1;
    options.statistics = std::make_shared<co::VulkanStatistics>();
    auto device = take(co::createVulkanDevice(options));
    auto a = take(device->createBuffer(4));
    auto b = take(device->createBuffer(4));
    auto k = take(device->createKernel(lutils::generated::add()));
    co::CommandList list;
    ok(list.upload(a, {1, 2, 3, 4}));
    ok(list.dispatch({k, {a, b}, {4, 3}, {4, 1, 1}}));
    auto token = take(list.readback(b));
    auto old = take(device->submit(list));
    // No wait on old: capacity one makes the next submission harvest it.
    ok(take(device->submit(list))->wait());
    auto slots = options.statistics->submissionSlotsCreated.load();
    auto pools = options.statistics->descriptorPoolsCreated.load();
    auto staging = options.statistics->stagingBuffersCreated.load();
    for (int i = 0; i < 8; ++i)
        ok(take(device->submit(list))->wait());
    CHECK(options.statistics->submissionSlotsCreated.load() == slots);
    CHECK(slots == 1);
    CHECK(options.statistics->descriptorPoolsCreated.load() == pools);
    CHECK(options.statistics->stagingBuffersCreated.load() == staging);
    auto *timed = dynamic_cast<co::VulkanCompletion *>(old.get());
    CHECK(timed);
    auto elapsed = timed->elapsedNanoseconds();
    CHECK(bool(elapsed) == options.enableTimestamps);
    CHECK(*take(old->readback(token)) == std::vector<co::Word>({5, 7, 9, 11}));
    ok(list.dispatch({k, {b, a}, {4, 1}, {4, 1, 1}}));
    ok(take(device->submit(list))->wait());
    CHECK(options.statistics->descriptorPoolsCreated.load() == pools + 1);
    if (elapsed)
        CHECK(take(timed->elapsedNanoseconds()) == elapsed.value());
    list = {};
    a.reset();
    b.reset();
    k.reset();
    device.reset();
    CHECK(take(old->ready()));
    CHECK(*take(old->readback(token)) == std::vector<co::Word>({5, 7, 9, 11}));

    options.maxCachedStagingBytes = 0;
    options.statistics = std::make_shared<co::VulkanStatistics>();
    device = take(co::createVulkanDevice(options));
    a = take(device->createBuffer(1));
    ok(list.upload(a, {123}));
    token = take(list.readback(a));
    for (int i = 0; i < 3; ++i)
        CHECK(*take(take(device->submit(list))->readback(token)) == std::vector<co::Word>{123});
    CHECK(options.statistics->submissionSlotsCreated.load() == 1);
    CHECK(options.statistics->stagingBuffersCreated.load() == 6);
}
#endif
int main(int argc, char **argv) {
    bool vulkan = argc > 1 && std::string{argv[1]} == "vulkan";
#ifdef LUTILS_TEST_VULKAN
    co::VulkanOptions options;
    options.deviceLocal = true;
    options.maxInFlight = 1;
    options.validationReport = std::make_shared<co::ValidationReport>();
    for (int i = 2; i < argc; ++i) {
        std::string arg{argv[i]};
        if (arg == "--require-hardware")
            options.requireHardware = true;
        else if (arg == "--validation")
            options.enableValidation = true;
        else if (arg == "--timestamps")
            options.enableTimestamps = true;
        else
            CHECK(false);
    }
    if (vulkan) {
        for (auto depth : {1u, 4u}) {
            options.maxInFlight = depth;
            auto device = take(co::createVulkanDevice(options));
            commands(*device);
            imageTransfer(*device);
            deviceLifetime(std::move(device));
        }
        reuse(options);
        options.reuseSubmissionResources = false;
        {
            auto device = take(co::createVulkanDevice(options));
            commands(*device);
            deviceLifetime(std::move(device));
        }
        options.maxInFlight = 0;
        CHECK(!co::createVulkanDevice(options));
        CHECK(options.validationReport->errors.load() == 0);
        std::cout << "validation errors=" << options.validationReport->errors.load()
                  << " warnings=" << options.validationReport->warnings.load() << '\n';
    } else
#else
    CHECK(!vulkan);
#endif
    {
        auto device = take(co::createCpuDevice());
        commands(*device);
        imageTransfer(*device);
        deviceLifetime(std::move(device));
    }
    std::cout << "ordered transfers, snapshots, lifetime and reuse passed\n";
}
