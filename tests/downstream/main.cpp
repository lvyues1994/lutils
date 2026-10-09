#include <lutils/compute/Runtime.hpp>
#include <lutils/erasure.hpp>
#include <lutils/image/Frame.hpp>
#include <stdexcept>
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
#ifdef WITH_OPS
    auto outDesc = desc;
    outDesc.format = im::nv12();
    outDesc.color.vertical = im::ChromaLocation::Midpoint;
    auto output = take(im::HostFrame::create(outDesc));
    unsigned char const raw[]{10, 20, 30, 40, 12, 22, 32, 42};
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x)
            im::row(input.view().planes[0], y)[x] = std::byte{raw[y * 4 + x]};
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
#endif
}
