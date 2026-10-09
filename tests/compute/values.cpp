#include <iostream>
#include <lutils/compute/Compute.hpp>
#include <stdexcept>
using namespace lutils::compute;
using namespace lutils::compute::kernel;
void check(bool value) {
    if (!value)
        throw std::runtime_error("value contract failed");
}
struct MixedPixel {
    template <Channel C>
    using ChannelType = std::conditional_t<C == Channel::R, std::uint8_t, float>;
    std::uint8_t r = 0;
    float g = 0.0f, b = 0.0f, a = 1.0f;
    template <Channel C> ChannelType<C> get() const {
        if constexpr (C == Channel::R)
            return r;
        else if constexpr (C == Channel::G)
            return g;
        else if constexpr (C == Channel::B)
            return b;
        else
            return a;
    }
    template <Channel C> void set(ChannelType<C> value) {
        if constexpr (C == Channel::R)
            r = value;
        else if constexpr (C == Channel::G)
            g = value;
        else if constexpr (C == Channel::B)
            b = value;
        else
            a = value;
    }
};
struct FailingUpload final : Device {
    std::unique_ptr<Device> inner = requiredDevice(createCpuDevice());
    DeviceInfo info() const override { return inner->info(); }
    lutils::Result<BufferHandle> createBuffer(std::size_t n) override {
        return inner->createBuffer(n);
    }
    lutils::Result<KernelHandle> createKernel(KernelSource source) override {
        return inner->createKernel(std::move(source));
    }
    lutils::Result<void> upload(BufferHandle const &, std::vector<Word> const &) override {
        return lutils::Error{lutils::ErrorCode::Device, "injected upload failure"};
    }
    lutils::Result<std::vector<Word>> download(BufferHandle const &b) override {
        return inner->download(b);
    }
    lutils::Result<std::shared_ptr<Completion>> submit(CommandList const &c) override {
        return inner->submit(c);
    }
};
void resourceContracts() {
    Backend backend{requiredDevice(createCpuDevice())};
    BufferResource<bool> flags{3};
    flags[0] = true;
    flags[1] = false;
    flags[2] = true;
    check(bool(backend.uploadBuffer(&flags)));
    flags[0] = false;
    check(bool(backend.downloadBuffer(&flags)) && flags[0] && !flags[1] && flags[2]);
    BufferResource<bool> moved{std::move(flags)};
    check(flags.size() == 0 && flags.extent() == 0 && !flags.identity());
    check(!backend.uploadBuffer(&flags));
    bool threw = false;
    try {
        flags[0] = true;
    } catch (std::out_of_range const &) {
        threw = true;
    }
    check(threw);
    check(bool(backend.downloadBuffer(&moved)) && moved[0]);
    check(!backend.device().createImage({static_cast<ImageFormat>(9999), 1, {1, 1, 1}}));
    check(!backend.device().createImage({ImageFormat::R8, 2, {UINT32_MAX, UINT32_MAX, 1}}));
    KernelSource invalid;
    invalid.name = "bad";
    invalid.abiVersion = 2;
    invalid.bindings = {Access::Write};
    check(!validate(invalid));
    invalid.parameterWords = 4;
    invalid.resources.push_back({});
    invalid.localSize = {Word{1} << 31, Word{1} << 31, 4};
    invalid.cpu = +[](kernel::Invocation, std::vector<CpuBuffer> const &,
                      std::vector<Word> const &) {};
    auto oversized = backend.device().createKernel(invalid);
    check(!oversized && oversized.error().code == lutils::ErrorCode::Unsupported);
    Backend failing{std::make_unique<FailingUpload>()};
    BufferResource<int> ints{1};
    ints[0] = 9;
    BufferBinding<int, 0> binding;
    binding.attach(&ints);
    check(!failing.uploadBuffer(&ints) && !failing.resolve(binding) &&
          !failing.downloadBuffer(&ints));
    check(ints[0] == 9);
}

int main() {
    try {
        resourceContracts();
        static_assert(IsPixel<MixedPixel>::value);
        static_assert(!IsPixel<int>::value);
        static_assert(sizeof(kernel::cpu::RGB565) == 2);
        static_assert(sizeof(kernel::cpu::RGB10A2) == 4);
        check(float(vec3{1.0f, 2.0f, 3.0f}["z"_sw]) == 3.0f);
        auto snapshot = vec4{1.0f, 2.0f, 3.0f, 4.0f}["wzyx"_sw];
        check(vec4{snapshot} == vec4{4.0f, 3.0f, 2.0f, 1.0f});
        check(dot(vec3{1.0f, 2.0f, 3.0f}, vec3{4.0f, 5.0f, 6.0f}) == 32.0f);
        check(cross(vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}) == vec3{0.0f, 0.0f, 1.0f});
        check(clamp(vec3{-1.0f, 0.5f, 2.0f}, 0.0f, 1.0f) == vec3{0.0f, 0.5f, 1.0f});
        Uniform<int, 0> a{2};
        Uniform<int, 1> b{3};
        check(a + b == 5 && a * b == 6 && b - a == 1 && b / a == 1);
        check(saturated<std::uint64_t>(INFINITY) == UINT64_MAX);
        check(saturated<std::int64_t>(-INFINITY) == INT64_MIN);
        check(saturated<unsigned>(NAN) == 0);
        MixedPixel p{128, 0.3f, 0.7f, 1.0f};
        auto channels = encodePixel<ImageFormat::RGBA32F>(p);
        check(channels.x == 128.0f / 255.0f && channels.y == 0.3f && channels.z == 0.7f &&
              channels.w == 1.0f);
        MixedPixel out;
        decodePixel<ImageFormat::RGBA32F>(out, channels);
        check(out.r == 128 && out.g == p.g && out.b == p.b && out.a == p.a);
        PackedPixel<31, 1, 0> wide;
        wide.set<Channel::R>(1.0f);
        check(wide.bits == 0x7fffffffu && wide.get<Channel::G>() == 0.0f);
        kernel::cpu::RGB565 packed;
        packed.set<Channel::R>(1.0f);
        packed.set<Channel::G>(0.5f);
        packed.set<Channel::B>(0.0f);
        check((packed.bits & 31u) == 31u && packed.get<Channel::A>() == 1.0f);
        check(std::abs(packed.get<Channel::G>() - 0.5f) <= 1.0f / 63.0f);
        kernel::cpu::BGRA8 bgra{0.1f, 0.2f, 0.3f, 1.0f};
        check(bgra.get<Channel::B>() < bgra.get<Channel::R>());
        std::cout << "shader value contracts passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
