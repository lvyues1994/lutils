#include "camera_rays.hpp"
#include "format_R16.hpp"
#include "format_R16F.hpp"
#include "format_R16I.hpp"
#include "format_R16Snorm.hpp"
#include "format_R16UI.hpp"
#include "format_R32F.hpp"
#include "format_R32I.hpp"
#include "format_R32UI.hpp"
#include "format_R8.hpp"
#include "format_R8I.hpp"
#include "format_R8Snorm.hpp"
#include "format_R8UI.hpp"
#include "format_RG16.hpp"
#include "format_RG16F.hpp"
#include "format_RG16I.hpp"
#include "format_RG16Snorm.hpp"
#include "format_RG16UI.hpp"
#include "format_RG32F.hpp"
#include "format_RG32I.hpp"
#include "format_RG32UI.hpp"
#include "format_RG8.hpp"
#include "format_RG8I.hpp"
#include "format_RG8Snorm.hpp"
#include "format_RG8UI.hpp"
#include "format_RGBA16.hpp"
#include "format_RGBA16F.hpp"
#include "format_RGBA16I.hpp"
#include "format_RGBA16Snorm.hpp"
#include "format_RGBA16UI.hpp"
#include "format_RGBA32F.hpp"
#include "format_RGBA32I.hpp"
#include "format_RGBA32UI.hpp"
#include "format_RGBA8.hpp"
#include "format_RGBA8I.hpp"
#include "format_RGBA8Snorm.hpp"
#include "format_RGBA8UI.hpp"
#include "game_of_life.hpp"
#include "layout.hpp"
#include "line.hpp"
#include "member_names.hpp"
#include "nearest_sample.hpp"
#include "sphere_tracer.hpp"
#include "typed_add.hpp"
#include "visualize_rays.hpp"
#include "volume.hpp"
#include <lutils/compute/Memory.hpp>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif
#include <iostream>
#include <stdexcept>
using lutils::Result;
using namespace lutils::compute;
using namespace lutils::compute::kernel;
template <class T> T require(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void require(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
void check(bool b) {
    if (!b)
        throw std::runtime_error("shader check failed");
}
void layouts(Backend &backend) {
    BufferResource<layout_test::Payload> input{7}, output{7};
    BufferResource<vec3> vectors{7};
    for (int i = 0; i < 7; ++i)
        input[i] = {i, {{1.0f, 2.0f, 3.0f}, float(i)}, {{vec2{1.0f}, vec2{2.0f}}}, double(i)};
    require(backend.uploadBuffer(&input));
    require(backend.uploadBuffer(&output));
    require(backend.uploadBuffer(&vectors));
    layout_test::Layout k;
    k.input.attach(&input);
    k.output.attach(&output);
    k.vectors.attach(&vectors);
    require(require(backend.execute(k, {7, 1, 1}))->wait());
    require(backend.downloadBuffer(&output));
    require(backend.downloadBuffer(&vectors));
    for (int i = 0; i < 7; ++i) {
        check(output[i].tag == i && output[i].inner.gain == float(i) + 2.0f);
        check(output[i].offsets[1] == vec2{5.0f, 6.0f} && output[i].weight == double(i) + 0.5);
        check(vectors[i] == vec3{3.0f, 2.0f, 1.0f});
    }
}
void memberNames(Backend &backend) {
    BufferResource<member_names::Value> input{5}, output{5};
    for (int i = 0; i < 5; ++i)
        input[i] = {{float(i), {{1u, 2u}}}, {3.0f, 4.0f, 5.0f}, 6u};
    require(backend.uploadBuffer(&input));
    require(backend.uploadBuffer(&output));
    member_names::Names shader;
    shader.source.attach(&input);
    shader.destination.attach(&output);
    shader.settings = member_names::Inner{2.0f, {{0u, 0u}}};
    require(require(backend.execute(shader, {5, 1, 1}))->wait());
    require(backend.downloadBuffer(&output));
    for (int i = 0; i < 5; ++i) {
        check(output[i].output.sample == float(i) + 2.0f);
        check(output[i].output.input == std::array<uint, 2>{{1u, 5u}});
        check(output[i].filter == vec3{float(i) + 5.0f, 4.0f, 5.0f});
        check(output[i].m0 == 13u);
    }
}
template <ImageFormat F, class K> void formatCopy(Backend &backend, bool es) {
    using Traits = GPUFormatTraits<F>;
    constexpr Word count = 9;
    ImageDesc desc{F, 2, {count, 1, 1}};
    if (es) {
        auto source = KernelTraits<K>::source();
        auto target = std::find_if(source.glsl.begin(), source.glsl.end(), [](auto const &s) {
            return s.target == GlslTarget::OpenGles31;
        });
        bool supported = F == ImageFormat::RGBA32F || F == ImageFormat::RGBA16F || F == ImageFormat::R32F ||
            F == ImageFormat::RGBA32UI || F == ImageFormat::RGBA16UI || F == ImageFormat::RGBA8UI || F == ImageFormat::R32UI ||
            F == ImageFormat::RGBA32I || F == ImageFormat::RGBA16I || F == ImageFormat::RGBA8I || F == ImageFormat::R32I ||
            F == ImageFormat::RGBA8 || F == ImageFormat::RGBA8Snorm;
        check(target != source.glsl.end());
        if (!supported) {
            auto rejected = backend.device().createKernel(std::move(source));
            check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
            return;
        }
        check(target->unsupportedReason.empty() && !target->text.empty());
    }
    auto input = require(backend.device().createImage(desc));
    auto output = require(backend.device().createImage(desc));
    std::vector<Word> words(require(imageWordCount(desc)), 0);
    auto *bytes = reinterpret_cast<unsigned char *>(words.data());
    for (Word i = 0; i < count; ++i) {
        using C = typename Traits::ChannelType;
        float value = (float(i) - 3.0f) * 0.2f;
        typename Traits::VectorType pixel;
        for (std::size_t c = 0; c < 4; ++c)
            pixel[c] = saturated<C>(double(value) + double(c) * 0.1);
        Traits::store(bytes + i * Traits::bytes, pixel);
    }
    K shader;
    shader.input.attach(input);
    shader.output.attach(output);
    CommandList commands;
    require(commands.upload(input, words));
    require(backend.record(commands, shader, {count, 1, 1}));
    auto token = require(commands.readback(output));
    auto done = require(backend.device().submit(commands));
    auto actual = require(done->readback(token));
    // Exact encoded values must survive a load/store roundtrip, including
    // normalized values.
    check(*actual == words);
}
void allFormats(Backend &backend, bool es) {
#define LUTILS_TEST_FORMAT(N, G, T, K, C, V)                                                       \
    formatCopy<ImageFormat::N, format_test::Copy##N>(backend, es);
    LUTILS_IMAGE_FORMATS(LUTILS_TEST_FORMAT)
#undef LUTILS_TEST_FORMAT
}
void dimensions(Backend &backend, bool es) {
    BufferResource<kernel::cpu::R16UI, Dim::D3> pixels{{5, 3, 3}};
    require(backend.uploadImage<kernel::gpu::R32UI>(&pixels));
    test_pdf::Volume k;
    k.image.attach(&pixels);
    require(require(backend.execute(k, {5, 3, 3}))->wait());
    require(backend.downloadImage<kernel::gpu::R32UI>(&pixels));
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x)
                check(pixels[{x, y, z}].get<Channel::R>() == x + 10 * y + 100 * z);
    if (es) {
        auto rejected = backend.device().createKernel(KernelTraits<test_pdf::Line>::source());
        check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
        check(!backend.device().createImage({kernel::gpu::RGBA8, 1, {9, 1, 1}}));
        return;
    }
    BufferResource<kernel::cpu::RG16I> line{9};
    require(backend.uploadImage<kernel::gpu::RG16I>(&line));
    test_pdf::Line l;
    l.image.attach(&line);
    require(require(backend.execute(l, {9, 1, 1}))->wait());
    require(backend.downloadImage<kernel::gpu::RG16I>(&line));
    for (int x = 0; x < 9; ++x) {
        check(line[x].get<Channel::R>() == x - 4);
        check(line[x].get<Channel::G>() == x + 2);
    }
}
void life(Backend &backend) {
    constexpr int width = 13, height = 11;
    BufferResource<kernel::cpu::R8UI, Dim::D2> a{{width, height}}, b{{width, height}};
    a[{5, 5}].set<Channel::R>(1);
    a[{6, 5}].set<Channel::R>(1);
    a[{7, 5}].set<Channel::R>(1);
    require(backend.uploadImage<kernel::gpu::R8UI>(&a));
    require(backend.uploadImage<kernel::gpu::R8UI>(&b));
    test_pdf::GameOfLife k;
    auto *in = &a;
    auto *out = &b;
    for (int step = 0; step < 4; ++step) {
        k.current.attach(in);
        k.next.attach(out);
        require(require(backend.execute(k, {width, height, 1}))->wait());
        require(backend.downloadImage<kernel::gpu::R8UI>(out));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                bool expected =
                    step % 2 == 0 ? (x == 6 && y >= 4 && y <= 6) : (y == 5 && x >= 5 && x <= 7);
                check((*out)[{x, y}].get<Channel::R>() == static_cast<unsigned>(expected));
            }
        std::swap(in, out);
    }
    k.neighbors[0] = {0, 0};
    check(!backend.execute(k, {width, height, 1}));
}
std::vector<unsigned char> raytrace(Backend &backend) {
    constexpr int width = 33, height = 25;
    BufferResource<test_pdf::Sphere> spheres{2};
    spheres[0] = {{0.0f, 0.0f, -3.0f, 0.8f}, {0.9f, 0.2f, 0.1f, 1.0f}};
    spheres[1] = {{1.0f, 0.3f, -4.0f, 0.5f}, {0.2f, 0.9f, 0.1f, 1.0f}};
    BufferResource<kernel::cpu::RGBA32F, Dim::D2> rays{{width, height}}, colors{{width, height}};
    BufferResource<kernel::cpu::BGRA8, Dim::D2> output{{width, height}};
    require(backend.uploadBuffer(&spheres));
    require(backend.uploadImage<kernel::gpu::RGBA32F>(&rays));
    require(backend.uploadImage<kernel::gpu::RGBA32F>(&colors));
    require(backend.uploadImage<kernel::gpu::RGBA8>(&output));
    test_pdf::CameraRays camera;
    camera.rays.attach(&rays);
    camera.aspect = float(width) / float(height);
    test_pdf::SphereTracer tracer;
    tracer.rays.attach(&rays);
    tracer.colors.attach(&colors);
    tracer.spheres.attach(&spheres);
    tracer.count = 2;
    test_pdf::VisualizeRays visualize;
    visualize.colors.attach(&colors);
    visualize.output.attach(&output);
    CommandList list;
    require(backend.record(list, camera, {width, height, 1}));
    require(backend.record(list, tracer, {width, height, 1}));
    require(backend.record(list, visualize, {width, height, 1}));
    auto done = require(backend.device().submit(list));
    // Recording takes a snapshot of uniform values and resolved resource handles.
    tracer.count = 0;
    tracer.spheres.attach(nullptr);
    require(done->wait());
    require(backend.downloadImage<kernel::gpu::RGBA8>(&output));
    std::vector<unsigned char> result;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (auto byte : output[{x, y}].data)
                result.push_back(byte);
    check(output[{width / 2, height / 2}].get<Channel::R>() > 0.2f);
    check(output[{0, 0}].get<Channel::B>() > output[{0, 0}].get<Channel::R>());
    return result;
}
void formats(Backend &backend) {
    // Includes normalized quantization, missing alpha, float special values and
    // trailing padding.
    BufferResource<kernel::cpu::R8UI> external{3};
    external[0] = kernel::cpu::R8UI{128};
    require(backend.uploadImage<kernel::gpu::RGBA8>(&external));
    ImageBinding<kernel::gpu::RGBA8, Dim::D1, kernel::cpu::R8UI, 0> binding;
    binding.attach(&external);
    auto raw = require(backend.device().download(require(backend.resolve(binding))));
    auto v = GPUFormatTraits<kernel::gpu::RGBA8>::load(raw.data());
    check(std::abs(v.x - 128.0f / 255.0f) < 1e-6f && v.y == 0.0f && v.z == 0.0f && v.w == 1.0f);
    BufferResource<kernel::cpu::RGBA32F> special{1};
    special[0] = kernel::cpu::RGBA32F{INFINITY, -INFINITY, NAN, -0.0f};
    require(backend.uploadImage<kernel::gpu::RGBA32F>(&special));
    require(backend.downloadImage<kernel::gpu::RGBA32F>(&special));
    check(std::isinf(special[0].data[0]) && special[0].data[0] > 0.0f);
    check(std::isinf(special[0].data[1]) && special[0].data[1] < 0.0f);
    check(std::isnan(special[0].data[2]) && std::signbit(special[0].data[3]));
    auto image = require(backend.device().createImage({kernel::gpu::R8UI, 1, {3, 1, 1}}));
    require(backend.device().upload(image, {0xff030201}));
    check(require(backend.device().download(image))[0] == 0x00030201);
}

void samplingAndCopy(Backend &backend) {
    BufferResource<kernel::cpu::RGBA8, Dim::D2> pixels{{3, 2}};
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels.data()[i].data = {{static_cast<std::uint8_t>(i * 47), 0, 255, 255}};
    BufferResource<vec2> positions{6};
    positions[0] = {-1, -1};
    positions[1] = {0.51f, 0.25f};
    positions[2] = {1.1f, 0.25f};
    positions[3] = {0.1f, 0.9f};
    positions[4] = {0.5f, 0.9f};
    positions[5] = {1, 1};
    BufferResource<vec4> output{6};
    require(backend.uploadImage<kernel::gpu::RGBA8>(&pixels));
    require(backend.uploadBuffer(&positions));
    require(backend.uploadBuffer(&output));
    sampler_test::Nearest shader;
    shader.image.attach(&pixels);
    shader.positions.attach(&positions);
    shader.output.attach(&output);
    require(require(backend.execute(shader, {6, 1, 1}))->wait());
    require(backend.downloadBuffer(&output));
    for (std::size_t i = 0; i < 6; ++i) {
        check(std::abs(output[i].x - float(i * 47) / 255.0f) < 0.00001f);
        check(output[i].y == 0 && output[i].z == 1 && output[i].w == 1);
    }
    auto imageHandle = require(backend.resolve(shader.image));
    auto outputHandle = require(backend.resolve(shader.output));
    shader.image.attach(imageHandle);
    shader.output.attach(outputHandle);
    check(!shader.image.resource() && !shader.output.resource());
    require(require(backend.execute(shader, {6, 1, 1}))->wait());
    require(backend.downloadBuffer(&output));
    for (std::size_t i = 0; i < 6; ++i)
        check(std::abs(output[i].x - float(i * 47) / 255.0f) < 0.00001f);
    shader.image.attach(outputHandle);
    check(!backend.execute(shader, {6, 1, 1})); // buffer cannot be sampled
    shader.image.attach(&pixels);
    check(!shader.image.residentBuffer());
    shader.image.attach(BufferHandle{});
    check(!backend.execute(shader, {6, 1, 1})); // no stale host binding
    shader.image.attach(imageHandle);
    shader.image.attach(nullptr);
    check(!backend.execute(shader, {6, 1, 1}));
    auto &device = backend.device();
    auto source = require(device.createImage({kernel::gpu::RGBA8, 2, {2, 3, 1}}));
    auto destination = require(device.createImage({kernel::gpu::RGBA8, 2, {4, 4, 1}}));
    require(device.upload(source, {1, 2, 3, 4, 5, 6}));
    CopyImageRegion copy{source, destination, 0, 1, 1, 1, 2, 2, std::array<float, 4>{{0, 1, 0, 1}}};
    CommandList commands;
    require(commands.copyImage(copy));
    require(require(device.submit(commands))->wait());
    auto data = require(device.download(destination));
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 4; ++x)
            check(data[y * 4 + x] == ((x >= 1 && x < 3 && y >= 1 && y < 3)
                                          ? static_cast<Word>(3 + (y - 1) * 2 + x - 1)
                                          : 0xff00ff00u));
    copy.source = destination;
    check(!commands.copyImage(copy));
    copy.source = source;
    copy.width = 3;
    check(!commands.copyImage(copy));
    copy.width = 0;
    check(!commands.copyImage(copy));
    copy.width = 2;
    copy.background = {{std::numeric_limits<float>::quiet_NaN(),0,0,0}};
    check(!commands.copyImage(copy));
    auto bad = KernelTraits<sampler_test::Nearest>::source();
    bad.bindings[0] = Access::Write;
    check(!device.createKernel(bad));
}
void residentBindings(Backend &backend, bool es) {
    auto &device = backend.device();
    BufferResource<float> host{3};
    host[0] = 1; host[1] = 2; host[2] = 3;
    require(backend.uploadBuffer(&host));
    auto input = require(device.createBuffer(3));
    auto output = require(device.createBuffer(3));
    auto memory = require(bufferMemory(input));
    test_pdf::FloatAdder shader;
    shader.A.attach(&host);
    shader.A.attach(require(device.importMemory(memory)));
    shader.B.attach(&host);
    shader.C.attach(output);
    check(!shader.A.resource() && shader.A.residentBuffer() == input);
    CommandList commands;
    std::vector<Word> words(3);
    for (std::size_t i = 0; i < 3; ++i)
        StorageCodec<float>::write(&words[i], float(i) + 4.0f);
    require(commands.upload(input, words));
    require(backend.record(commands, shader, {3, 1, 1}));
    auto token = require(commands.readback(output));
    std::weak_ptr<Buffer> lifetime = input;
    shader = {};
    input.reset();
    memory.reset();
    output.reset();
    check(!lifetime.expired());
    auto done = require(device.submit(commands));
    auto result = require(done->readback(token));
    for (std::size_t i = 0; i < 3; ++i)
        check(StorageCodec<float>::read(&(*result)[i]) == 5.0f + float(i) * 2.0f);
    commands = {};
    done.reset();
    check(lifetime.expired());

    shader.A.attach(&host);
    shader.B.attach(&host);
    auto destination = require(device.createBuffer(3));
    shader.C.attach(destination);
    shader.A.attach(require(device.createBufferBytes(6)));
    check(!backend.execute(shader, {3, 1, 1})); // element stride
    shader.A.attach(require(device.createImage({kernel::gpu::RGBA8, 2, {1, 1, 1}})));
    check(!backend.execute(shader, {1, 1, 1})); // image is not a buffer
    shader.A.attach(destination);
    check(!backend.execute(shader, {3, 1, 1})); // writable alias
    shader.A.attach(&host);
    check(!shader.A.residentBuffer());
    shader.A.attach(BufferHandle{});
    check(!backend.execute(shader, {3, 1, 1}));
    shader.A.attach(&host);

    struct ReadOnly final : Buffer {
        std::size_t wordCount() const noexcept override { return 3; }
        bool writable() const noexcept override { return false; }
    };
    shader.C.attach(std::make_shared<ReadOnly>());
    CommandList rejected;
    check(!backend.record(rejected, shader, {3, 1, 1}));
    check(rejected.commands().empty());
    shader.C.attach(destination);
    auto foreign = require(createCpuDevice());
    shader.A.attach(require(foreign->createBuffer(3)));
    CommandList invalidDevice;
    require(invalidDevice.upload(destination, {1, 2, 3}));
    require(backend.record(invalidDevice, shader, {3, 1, 1}));
    check(!device.submit(invalidDevice));
    check(require(device.download(destination)) == std::vector<Word>(3, 0));

    if (es)
        return; // Dedicated dimensions() case verifies the GLES 1D rejection.
    test_pdf::Line line;
    auto validImage = require(device.createImage({kernel::gpu::RG16I, 1, {3, 1, 1}}));
    line.image.attach(validImage);
    line.image.attach(require(device.createImage({kernel::gpu::RG16I, 2, {3, 1, 1}})));
    check(!backend.execute(line, {3, 1, 1})); // dimensions
    line.image.attach(require(device.createImage({kernel::gpu::RGBA8, 1, {3, 1, 1}})));
    check(!backend.execute(line, {3, 1, 1})); // format
    line.image.attach(destination);
    check(!backend.execute(line, {3, 1, 1})); // buffer is not an image
    BufferResource<kernel::cpu::RG16I> pixels{3};
    require(backend.uploadImage<kernel::gpu::RG16I>(&pixels));
    line.image.attach(&pixels);
    check(!line.image.residentBuffer());
    line.image.attach(BufferHandle{});
    check(!backend.execute(line, {3, 1, 1}));
    line.image.attach(validImage);
    line.image.attach(nullptr);
    check(!backend.execute(line, {3, 1, 1}));
}
#include "TestDevice.hpp"
int main(int argc, char **argv) {
    try {
        auto device = createCpuDevice();
        auto selected = test_device::name(argc, argv);
        bool es = selected == "gles";
        if (test_device::gl(selected))
            device = test_device::createGl(argc, argv);
#ifdef LUTILS_TEST_VULKAN
        auto validation = std::make_shared<ValidationReport>();
        if (selected == "vulkan") {
            VulkanOptions options;
            options.enableValidation = argc > 2;
            options.validationReport = validation;
            options.requireHardware = argc > 3;
            device = createVulkanDevice(options);
        }
        (void)argv;
#else
        (void)argc;
        (void)argv;
#endif
        Backend backend{require(std::move(device))};
        memberNames(backend);
        residentBindings(backend, es);
        samplingAndCopy(backend);
        BufferResource<float> a{21}, b{21}, c{21};
        for (int i = 0; i < 21; ++i) {
            a[i] = float(i);
            b[i] = float(i) * 0.25f;
        }
        require(backend.uploadBuffer(&a));
        require(backend.uploadBuffer(&b));
        require(backend.uploadBuffer(&c));
        test_pdf::FloatAdder k;
        k.A.attach(&a);
        k.B.attach(&b);
        k.C.attach(&c);
        k.scale = 2.0f;
        require(require(backend.execute(k, {21, 1, 1}))->wait());
        require(backend.downloadBuffer(&c));
        k.local_size.x = 8;
        check(!backend.execute(k, {21, 1, 1}));
        k.local_size.x = 256;
        k.A.attach(nullptr);
        check(!backend.execute(k, {21, 1, 1}));
        for (int i = 0; i < 21; ++i)
            check(c[i] == float(i) * 2.5f);
        if (backend.device().info().capabilities.float64)
            layouts(backend);
        else {
            auto rejected =
                backend.device().createKernel(KernelTraits<layout_test::Layout>::source());
            check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
            std::cout << "Float64 unavailable: checked rejection; continuing "
                         "remaining shaders\n";
        }
        allFormats(backend, es);
        dimensions(backend, es);
        if (es) {
            auto rejected =
                backend.device().createKernel(KernelTraits<test_pdf::GameOfLife>::source());
            check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
        } else {
            life(backend);
            formats(backend);
        }
        auto actual = raytrace(backend);
        Backend reference{require(createCpuDevice())};
        auto expected = raytrace(reference);
        check(expected.size() == actual.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            check(std::abs(int(expected[i]) - int(actual[i])) <= 1);
#ifdef LUTILS_TEST_VULKAN
        check(validation->errors == 0 && validation->warnings == 0);
#endif
        std::cout << "shader tests passed: " << backend.device().info().name << "\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
