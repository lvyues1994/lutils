#include <cstring>
#include <limits>
#include <lutils/image/Conversion.hpp>
#include <nv12_to_rgba.hpp>
#include <packed422_to_nv12.hpp>

namespace lutils::image {
namespace {
Error invalid(char const *message) { return {ErrorCode::InvalidArgument, message}; }
Error unsupported(char const *message) { return {ErrorCode::Unsupported, message}; }
bool nativeLittleEndian() {
    compute::Word one = 1;
    unsigned char first = 0;
    std::memcpy(&first, &one, 1);
    return first == 1;
}
Result<void> compatible(FrameDesc const &a, FrameDesc const &b) {
    if (a.width != b.width || a.height != b.height)
        return unsupported("conversion cannot resize the luma plane");
    if (a.scan != Scan::Progressive || b.scan != Scan::Progressive)
        return unsupported("conversion requires progressive frames");
    if (a.color.primaries != b.color.primaries || a.color.transfer != b.color.transfer)
        return unsupported("conversion preserves color primaries and transfer");
    return {};
}
bool overlap(ConstFrameView const &source, FrameView const &destination) {
    for (auto const &a : source.planes)
        for (auto const &b : destination.planes) {
            // Allocation identity may have different base pointers in external views.
            auto x = reinterpret_cast<std::uintptr_t>(a.base);
            auto y = reinterpret_cast<std::uintptr_t>(b.base);
            if (x <= y ? y - x < a.capacity : x - y < b.capacity)
                return true;
        }
    return false;
}
} // namespace
Result<DeviceLayout> deviceLayout(FrameDesc const &desc) {
    auto shapes = geometry(desc);
    if (!shapes)
        return shapes.error();
    DeviceLayout layout{{}, 0};
    for (auto const &shape : shapes.value()) {
        auto stride = shape.rowBytes / 4 + (shape.rowBytes % 4 ? 1u : 0u);
        if (shape.rowBytes > UINT32_MAX || stride > UINT32_MAX || shape.rows > UINT32_MAX ||
            (shape.rows && stride > (UINT32_MAX - layout.words) / shape.rows))
            return Error{ErrorCode::Overflow, "device image layout exceeds 32-bit indexing"};
        layout.planes.push_back(
            {static_cast<std::uint32_t>(layout.words), static_cast<std::uint32_t>(stride),
             static_cast<std::uint32_t>(shape.rows), static_cast<std::uint32_t>(shape.rowBytes)});
        layout.words += stride * shape.rows;
    }
    return layout;
}
Result<DeviceFrame> DeviceFrame::create(compute::Device &device, FrameDesc desc) {
    auto layout = deviceLayout(desc);
    if (!layout)
        return layout.error();
    auto buffer = device.createBuffer(layout.value().words);
    if (!buffer)
        return buffer.error();
    DeviceFrame frame;
    frame.desc_ = std::move(desc);
    frame.layout_ = std::move(layout).value();
    frame.buffer_ = std::move(buffer).value();
    return frame;
}
Result<void> recordUpload(compute::CommandList &commands, ConstFrameView const &source,
                          DeviceFrame const &destination) {
    auto valid = validate(source);
    if (!valid)
        return valid;
    if (!(source.desc == destination.description()))
        return invalid("upload frame descriptions differ");
    std::vector<compute::Word> words(destination.layout().words, 0u);
    bool const directCopy = nativeLittleEndian();
    for (std::size_t i = 0; i < source.planes.size(); ++i) {
        auto const &p = destination.layout().planes[i];
        for (std::uint32_t y = 0; y < p.rows && p.rowBytes; ++y) {
            auto const *bytes = row(source.planes[i], y);
            if (directCopy) {
                std::memcpy(words.data() + p.offsetWords + y * p.strideWords, bytes, p.rowBytes);
                continue;
            }
            for (std::uint32_t x = 0; x < p.rowBytes; ++x)
                words[p.offsetWords + y * p.strideWords + x / 4u] |=
                    std::to_integer<std::uint32_t>(bytes[x]) << ((x % 4u) * 8u);
        }
    }
    return commands.upload(destination.buffer(), std::move(words));
}
Result<FrameReadback> recordReadback(compute::CommandList &commands, DeviceFrame const &source) {
    auto token = commands.readback(source.buffer());
    if (!token)
        return token.error();
    FrameReadback result;
    result.desc_ = source.description();
    result.layout_ = source.layout();
    result.token_ = std::move(token).value();
    return result;
}
Result<void> FrameReadback::copyTo(compute::Completion &completion,
                                   FrameView const &destination) const {
    auto valid = validate(destination);
    if (!valid)
        return valid;
    if (!(desc_ == destination.desc))
        return invalid("download frame descriptions differ");
    auto words = completion.readback(token_);
    if (!words)
        return words.error();
    if (!words.value() || words.value()->size() != layout_.words)
        return invalid("readback size does not match frame layout");
    bool const directCopy = nativeLittleEndian();
    for (std::size_t i = 0; i < destination.planes.size(); ++i) {
        auto const &p = layout_.planes[i];
        for (std::uint32_t y = 0; y < p.rows && p.rowBytes; ++y) {
            auto *bytes = row(destination.planes[i], y);
            if (directCopy) {
                std::memcpy(bytes, words.value()->data() + p.offsetWords + y * p.strideWords,
                            p.rowBytes);
                continue;
            }
            for (std::uint32_t x = 0; x < p.rowBytes; ++x)
                bytes[x] = static_cast<std::byte>(
                    ((*words.value())[p.offsetWords + y * p.strideWords + x / 4u] >>
                     ((x % 4u) * 8u)) &
                    255u);
        }
    }
    return {};
}
Result<void> upload(compute::Device &device, ConstFrameView const &source,
                    DeviceFrame const &destination) {
    compute::CommandList commands;
    auto recorded = recordUpload(commands, source, destination);
    if (!recorded)
        return recorded;
    auto done = device.submit(commands);
    return done ? done.value()->wait() : Result<void>{done.error()};
}
Result<void> download(compute::Device &device, DeviceFrame const &source,
                      FrameView const &destination) {
    auto valid = validate(destination);
    if (!valid)
        return valid;
    if (!(source.description() == destination.desc))
        return invalid("download frame descriptions differ");
    compute::CommandList commands;
    auto readback = recordReadback(commands, source);
    if (!readback)
        return readback.error();
    auto done = device.submit(commands);
    return done ? readback.value().copyTo(*done.value(), destination) : Result<void>{done.error()};
}
Result<ConversionPlan> ConversionPlan::prepare(compute::Device &device, FrameDesc source,
                                               FrameDesc destination) {
    auto compatibleFrames = compatible(source, destination);
    if (!compatibleFrames)
        return compatibleFrames.error();
    auto src = deviceLayout(source);
    if (!src)
        return src.error();
    auto dst = deviceLayout(destination);
    if (!dst)
        return dst.error();
    ConversionPlan plan;
    compute::KernelSource code;
    bool const uyvy = source.format == uyvy422();
    if ((source.format == yuyv422() || uyvy) && destination.format == nv12()) {
        auto expected = source.color;
        expected.vertical = ChromaLocation::Midpoint;
        if (!(destination.color == expected) || source.color.vertical != ChromaLocation::Cosited ||
            source.color.horizontal == ChromaLocation::Unknown)
            return unsupported("422 to 420 requires known horizontal siting, cosited input rows "
                               "and midpoint output rows");
        if (std::uint64_t{source.height} + (std::uint64_t{source.height} + 1) / 2 > UINT32_MAX)
            return Error{ErrorCode::Overflow, "conversion dispatch height overflows"};
        code = compute::KernelTraits<kernels::Packed422ToNv12>::source();
        kernels::Packed422Params p{source.width,
                                   source.height,
                                   src.value().planes[0].strideWords,
                                   dst.value().planes[0].strideWords,
                                   dst.value().planes[1].offsetWords,
                                   dst.value().planes[1].strideWords,
                                   uyvy ? 8u : 0u,
                                   uyvy ? 0u : 8u,
                                   uyvy ? 16u : 24u};
        plan.extent_ = {dst.value().planes[0].strideWords,
                        source.height + (source.height + 1u) / 2u, 1};
        kernels::Packed422ToNv12 kernel;
        kernel.parameters = p;
        plan.parameters_ =
            compute::KernelTraits<kernels::Packed422ToNv12>::pack(kernel, plan.extent_);
    } else if (source.format == nv12() && destination.format == rgba8()) {
        if (source.color.matrix != Matrix::Bt601 || source.color.range != Range::Limited ||
            source.color.horizontal == ChromaLocation::Unknown ||
            source.color.vertical != ChromaLocation::Midpoint ||
            destination.color.matrix != Matrix::Identity ||
            destination.color.range != Range::Full ||
            destination.color.horizontal != ChromaLocation::Unknown ||
            destination.color.vertical != ChromaLocation::Unknown)
            return unsupported("NV12 to RGBA requires BT.601 limited, known horizontal/midpoint "
                               "vertical siting and full-range RGB output");
        code = compute::KernelTraits<kernels::Nv12ToRgba>::source();
        kernels::RgbParams p{source.width,
                             source.height,
                             src.value().planes[0].strideWords,
                             src.value().planes[1].offsetWords,
                             src.value().planes[1].strideWords,
                             dst.value().planes[0].strideWords};
        plan.extent_ = {source.width, source.height, 1};
        kernels::Nv12ToRgba kernel;
        kernel.parameters = p;
        plan.parameters_ = compute::KernelTraits<kernels::Nv12ToRgba>::pack(kernel, plan.extent_);
    } else
        return unsupported("no kernel for this format conversion");

    auto kernel = device.createKernel(std::move(code));
    if (!kernel)
        return kernel.error();
    plan.kernel_ = std::move(kernel).value();
    plan.source_ = std::move(source);
    plan.destination_ = std::move(destination);
    return plan;
}
Result<void> ConversionPlan::record(compute::CommandList &commands, DeviceFrame const &source,
                                    DeviceFrame const &destination) const {
    if (!kernel_ || !(source.description() == source_) ||
        !(destination.description() == destination_))
        return invalid("frames do not match prepared conversion");
    return commands.dispatch(
        {kernel_, {source.buffer(), destination.buffer()}, parameters_, extent_});
}
Result<void> ConversionPlan::run(compute::Device &device, ConstFrameView const &source,
                                 FrameView const &destination) const {
    auto a = validate(source);
    if (!a)
        return a;
    auto b = validate(destination);
    if (!b)
        return b;
    if (!(source.desc == source_) || !(destination.desc == destination_))
        return invalid("frames do not match prepared conversion");
    if (overlap(source, destination))
        return invalid("source and destination allocations overlap");
    auto src = DeviceFrame::create(device, source.desc);
    if (!src)
        return src.error();
    auto dst = DeviceFrame::create(device, destination.desc);
    if (!dst)
        return dst.error();
    compute::CommandList commands;
    auto copied = recordUpload(commands, source, src.value());
    if (!copied)
        return copied;
    auto recorded = record(commands, src.value(), dst.value());
    if (!recorded)
        return recorded;
    auto readback = recordReadback(commands, dst.value());
    if (!readback)
        return readback.error();
    auto done = device.submit(commands);
    if (!done)
        return done.error();
    return readback.value().copyTo(*done.value(), destination);
}
} // namespace lutils::image
