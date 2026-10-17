#include <lutils/Expected.hpp>
#include <lutils/compute/Runtime.hpp>
#include <lutils/erasure.hpp>
#include <lutils/image/CpuConversionExecutor.hpp>
#include <lutils/image/CpuTuning.hpp>
#include <lutils/image/Frame.hpp>
#include <lutils/image/ImageResource.hpp>
#include <lutils/image/SimdConversion.hpp>
#include <lutils/simd.hpp>
#include <stdexcept>
#ifdef __ANDROID__
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <lutils/image/AndroidImage.hpp>
#endif
#ifdef WITH_SHADER
#include <consumer_shader.hpp>
#endif
#ifdef WITH_OPS
#include <lutils/image/Conversion.hpp>
#endif
namespace te = lutils::erasure;
namespace co = lutils::compute;
namespace im = lutils::image;
template <class M> struct IValue : te::Interface<IValue, M> {
    using IValue::Interface::Interface;
    virtual int value() const { return te::value(*this).value(); }
};
struct Value {
    int value() const { return 42; }
};
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(lutils::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
void check(bool value) {
    if (!value)
        throw std::runtime_error("consumer result mismatch");
}
int main() {
#ifdef __ANDROID__
    check(!co::DmaBufMemory::import(-1));
    check(!im::importAndroidImage(nullptr));
    check(!im::adoptAndroidImage(im::AImageOwner{}, {}));
#endif
    auto expectedValue =
        lutils::expected<int, std::string>{21}.transform([](int x) { return x * 2; });
    check(expectedValue.has_value() and expectedValue.value() == 42);
    auto expectedError = lutils::expected<void, int>{lutils::unexpect, 9};
    check(expectedError.error() == 9);
    te::Any<IValue> value{Value{}};
    check(value.value() == 42);
    auto device = take(co::createCpuDevice());
#ifdef WITH_GPU
    device = take(co::createVulkanDevice());
#endif
    im::FrameDesc desc{2,
                       2,
                       im::uyvy422(),
                       {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                        im::ChromaLocation::Cosited, im::Primaries::Bt601_625, im::Transfer::Bt709},
                       im::Scan::Progressive};
    auto input = take(im::HostFrame::create(desc));
    auto regionOutput = take(im::HostFrame::create(desc));
    auto cropPlan = take(im::RegionPlan::crop(desc, {0, 0, 2, 2}));
    auto regions = im::createCpuRegionExecutor();
    take(regions->run(cropPlan, take(im::ImageResource::borrow(input.view())),
                      take(im::ImageResource::borrow(regionOutput.view()))));
    auto outDesc = desc;
    outDesc.format = im::nv12();
    outDesc.color.vertical = im::ChromaLocation::Midpoint;
    auto output = take(im::HostFrame::create(outDesc));
    unsigned char const raw[]{10, 20, 30, 40, 12, 22, 32, 42};
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x)
            im::row(input.view().planes[0], y)[x] = std::byte{raw[y * 4 + x]};
    using V = lutils::simd::Batch<std::int32_t, lutils::simd::abi::scalar>;
    check(lutils::simd::all(V{20} + V{22} == V{42}));
    take(im::convertUyvyToNv12(im::readOnly(input.view()), output.view()));
    check(im::row(output.view().planes[0], 0)[0] == std::byte{20});
    check(im::row(output.view().planes[0], 1)[1] == std::byte{42});
    check(im::row(output.view().planes[1], 0)[0] == std::byte{11});
    check(im::row(output.view().planes[1], 0)[1] == std::byte{31});
    auto cpuPlan = take(im::CpuConversionPlan::prepare(desc, outDesc));
    auto tuned = take(im::tuneCpuConversion(im::readOnly(input.view()), output.view()));
    take(tuned.plan.run(im::readOnly(input.view()), output.view()));
    auto cpu = im::createCpuConversionExecutor();
    take(cpu->run(cpuPlan, take(im::ImageResource::borrow(im::readOnly(input.view()))),
                  take(im::ImageResource::borrow(output.view()))));
    check(im::row(output.view().planes[1], 0)[1] == std::byte{31});
    auto rgbDesc = outDesc;
    rgbDesc.format = im::rgba8();
    rgbDesc.color.matrix = im::Matrix::Identity;
    rgbDesc.color.range = im::Range::Full;
    rgbDesc.color.horizontal = im::ChromaLocation::Unknown;
    rgbDesc.color.vertical = im::ChromaLocation::Unknown;
    auto rgb = take(im::HostFrame::create(rgbDesc));
    take(im::convertNv12ToRgba(im::readOnly(output.view()), rgb.view()));
    check(im::row(rgb.view().planes[0], 0)[3] == std::byte{255});
#ifdef WITH_OPS
    auto plan = take(im::ConversionPlan::prepare(*device, desc, outDesc));
    take(plan.run(*device, im::readOnly(input.view()), output.view()));
    check(im::row(output.view().planes[0], 0)[0] == std::byte{20});
    check(im::row(output.view().planes[0], 1)[1] == std::byte{42});
    check(im::row(output.view().planes[1], 0)[0] == std::byte{11});
    check(im::row(output.view().planes[1], 0)[1] == std::byte{31});
#endif
#ifdef WITH_SHADER
    co::Backend backend{std::move(device)};
    co::BufferResource<float> outputBuffer{7};
    take(backend.uploadBuffer(&outputBuffer));
    consumer::Add kernel;
    kernel.output.attach(&outputBuffer);
    take(take(backend.execute(kernel, {7, 1, 1}))->wait());
    take(backend.downloadBuffer(&outputBuffer));
    for (int i = 0; i < 7; ++i)
        check(outputBuffer[i] == float(i) + FACTOR);
    auto resident = take(backend.device().createBuffer(7));
    kernel.output.attach(resident);
    take(take(backend.execute(kernel, {7, 1, 1}))->wait());
    auto words = take(backend.device().download(resident));
    for (std::size_t i = 0; i < words.size(); ++i)
        check(co::StorageCodec<float>::read(&words[i]) == float(i) + FACTOR);
#endif
}
