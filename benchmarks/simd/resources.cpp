#include "Benchmark.hpp"
#include <cstring>
#include <iomanip>
#include <lutils/image/CpuConversionExecutor.hpp>
#if __has_include(<lutils/image/CpuTuning.hpp>)
#include <lutils/image/CpuTuning.hpp>
#define LUTILS_BENCH_TUNING 1
#endif
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <lutils/compute/LinuxDmaBuf.hpp>
#include <sys/ioctl.h>
#endif
#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <lutils/compute/AndroidHardwareBufferImage.hpp>
#endif
namespace im = lutils::image;
namespace co = lutils::compute;
namespace s = lutils::simd;
using namespace benchmark;
namespace {
struct MappedResource {
    im::ConstFrameView read;
    im::FrameView write;
    std::vector<std::unique_ptr<co::MemoryMapping>> linear;
    std::unique_ptr<co::NativeImageMapping> native;
    void finish() {
        if (native) {
            take(native->finish());
            native.reset();
        }
        for (auto &m : linear) {
            take(m->finish());
            m.reset(); // DMA munmap is in the mapping destructor.
        }
        linear.clear();
    }
};
MappedResource map(im::ImageResource const &image, co::Access access) {
    MappedResource out{im::ConstFrameView{image.description(), {}},
                       im::FrameView{image.description(), {}},
                       {},
                       {}};
    if (image.nativeStorage()) {
        out.native = take(image.nativeStorage()->map(access));
        auto const &p = out.native->planes().at(0);
        check(p.pixelStride == 4 && p.rowStride <= std::size_t(PTRDIFF_MAX));
        out.read.planes.push_back(
            {p.data, p.capacity, 0, static_cast<std::ptrdiff_t>(p.rowStride)});
        if (access != co::Access::Read)
            out.write.planes.push_back({take(out.native->writablePlane(0)), p.capacity, 0,
                                        static_cast<std::ptrdiff_t>(p.rowStride)});
    } else {
        for (auto const &object : image.objects())
            out.linear.push_back(take(object->map(access)));
        for (auto const &p : image.planes()) {
            auto &mapping = out.linear.at(p.object);
            auto bytes = image.objects().at(p.object)->byteCount();
            out.read.planes.push_back({mapping->data(), bytes, p.offset, p.stride});
            if (access != co::Access::Read)
                out.write.planes.push_back(
                    {take(mapping->writableData()), bytes, p.offset, p.stride});
        }
    }
    return out;
}
co::MemoryHandle allocate(std::size_t bytes, std::string const &kind, int heap) {
    if (kind == "va") {
        auto storage = std::make_shared<std::vector<std::byte>>(bytes);
        return take(co::borrowMemory(storage->data(), storage->size(), storage));
    }
#ifdef __ANDROID__
    if (kind == "ahb" || kind == "ahb-rgba")
        return take(co::AndroidHardwareBufferMemory::allocate(bytes));
#endif
#if defined(__linux__)
    if (kind == "dma") {
        dma_heap_allocation_data request{};
        request.len = bytes;
        request.fd_flags = O_RDWR | O_CLOEXEC;
        if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &request) < 0)
            throw std::runtime_error(std::string("DMA_HEAP_IOCTL_ALLOC: ") + std::strerror(errno));
        co::FileDescriptor fd{static_cast<int>(request.fd)};
        return take(co::DmaBufMemory::import(fd.get()));
    }
#else
    (void)heap;
#endif
    throw std::runtime_error("requested resource is unavailable in this build");
}
im::ImageResource resource(im::FrameDesc const &desc, std::size_t alignment, bool split,
                           std::string const &kind, int heap) {
#ifdef __ANDROID__
    if (kind == "ahb-rgba" && desc.format == im::rgba8()) {
        AHardwareBuffer_Desc d{};
        d.width = desc.width;
        d.height = desc.height;
        d.layers = 1;
        d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        d.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
        AHardwareBuffer *raw = nullptr;
        check(AHardwareBuffer_allocate(&d, &raw) == 0);
        auto owner = std::shared_ptr<AHardwareBuffer>{raw, AHardwareBuffer_release};
        return take(im::ImageResource::native(take(co::AndroidHardwareBufferImage::import(raw)),
                                              desc.color));
    }
#endif
    auto geometry = take(im::geometry(desc));
    std::vector<im::PlaneLayout> planes;
    std::vector<co::MemoryHandle> objects;
    std::size_t total = 0;
    for (auto const &p : geometry) {
        auto stride = (p.rowBytes + alignment - 1) / alignment * alignment;
        planes.push_back(
            {split ? objects.size() : 0, split ? 0 : total, static_cast<std::ptrdiff_t>(stride)});
        if (split)
            objects.push_back(allocate(stride * p.rows, kind, heap));
        total += stride * p.rows;
    }
    if (!split)
        objects.push_back(allocate(total, kind, heap));
    return take(im::ImageResource::linear(desc, std::move(objects), std::move(planes)));
}
void run(std::string const &kind, int heap, bool smoke, Clock::time_point deadline) {
    struct Shape {
        std::uint32_t width, height;
        std::size_t alignment;
    };
    auto shapes = smoke ? std::vector<Shape>{{66, 5, 64}}
                        : std::vector<Shape>{{640, 480, 1}, {1920, 1536, 1}, {642, 481, 64}};
    auto executor = im::createCpuConversionExecutor();
    for (auto shape : shapes)
        for (bool rgba : {false, true}) {
            if (kind == "ahb-rgba" && !rgba)
                continue;
            for (bool split : {false, true}) {
                im::FrameDesc desc{
                    shape.width,
                    shape.height,
                    rgba ? im::nv12() : im::uyvy422(),
                    {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                     rgba ? im::ChromaLocation::Midpoint : im::ChromaLocation::Cosited}};
                auto dst = desc;
                dst.format = rgba ? im::rgba8() : im::nv12();
                if (rgba)
                    dst.color = {im::Matrix::Identity, im::Range::Full};
                else
                    dst.color.vertical = im::ChromaLocation::Midpoint;
                auto input = resource(desc, shape.alignment, split, kind, heap);
                auto output = resource(dst, shape.alignment, split, kind, heap);
                auto expected = take(im::HostFrame::create(dst));
                auto g = take(im::geometry(desc)), og = take(im::geometry(dst));
                {
                    auto m = map(input, co::Access::Write);
                    std::uint32_t state = 93721;
                    for (std::size_t p = 0; p < g.size(); ++p)
                        for (std::size_t y = 0; y < g[p].rows; ++y)
                            for (std::size_t x = 0; x < g[p].rowBytes; ++x) {
                                state ^= state << 13;
                                state ^= state >> 17;
                                state ^= state << 5;
                                im::row(m.write.planes[p], y)[x] = std::byte(state & 255);
                            }
                    imageReference(m.read, expected.view(), rgba);
                    m.finish();
                }
                std::vector<char const *> targets{"automatic"};
#ifdef LUTILS_BENCH_TUNING
                targets.push_back("tuned");
#endif
                for (auto target : targets) {
                    auto plan = take(im::CpuConversionPlan::prepare(desc, dst));
                    std::int64_t calibration = 0;
                    std::ptrdiff_t sourceStride = 0, destinationStride = 0;
                    {
                        auto a = map(input, co::Access::Read), b = map(output, co::Access::Write);
                        sourceStride = a.read.planes[0].stride;
                        destinationStride = b.write.planes[0].stride;
#ifdef LUTILS_BENCH_TUNING
                        if (std::string(target) == "tuned") {
                            auto t = take(im::tuneCpuConversion(a.read, b.write));
                            plan = std::move(t.plan);
                            calibration = t.elapsed.count();
                        }
#endif
                        b.finish();
                        a.finish();
                    }
                    std::string fields =
                        "\"operation\":\"" + std::string(rgba ? "nv12_rgba" : "uyvy_nv12") +
                        "\",\"target\":\"" + target + "\",\"selected_target\":\"" +
                        s::target_name(plan.target()) + "\",\"memory\":\"" + kind +
                        "\",\"split\":" + (split ? "true" : "false") +
                        ",\"width\":" + std::to_string(shape.width) +
                        ",\"height\":" + std::to_string(shape.height) +
                        ",\"row_alignment\":" + std::to_string(shape.alignment) +
                        ",\"source_stride\":" + std::to_string(sourceStride) +
                        ",\"destination_stride\":" + std::to_string(destinationStride) +
                        ",\"elements\":" +
                        std::to_string(std::uint64_t(shape.width) * shape.height) +
                        ",\"calibration_ns\":" + std::to_string(calibration);
                    auto poison = [&](im::FrameView const &view) {
                        auto ref = expected.view();
                        for (std::size_t p = 0; p < og.size(); ++p)
                            for (std::size_t y = 0; y < og[p].rows; ++y)
                                for (std::size_t x = 0; x < og[p].rowBytes; ++x)
                                    im::row(view.planes[p], y)[x] =
                                        im::row(ref.planes[p], y)[x] ^ std::byte{1};
                    };
                    auto verify = [&](im::ConstFrameView const &view) {
                        auto ref = expected.view();
                        for (std::size_t p = 0; p < og.size(); ++p)
                            for (std::size_t y = 0; y < og[p].rows; ++y)
                                check(std::equal(im::row(ref.planes[p], y),
                                                 im::row(ref.planes[p], y) + og[p].rowBytes,
                                                 im::row(view.planes[p], y)));
                    };
                    auto execute = [&]() {
                        take(executor->run(plan, input, output));
                        return std::uint64_t{1};
                    };
                    auto verifyOutput = [&](std::uint64_t) {
                        auto m = map(output, co::Access::Read);
                        verify(m.read);
                        m.finish();
                    };
                    auto validate = [&]() {
                        {
                            auto m = map(output, co::Access::Write);
                            poison(m.write);
                            m.finish();
                        }
                        consumed = execute();
                        verifyOutput(0);
                    };
                    measure(fields + ",\"scope\":\"executor_run\"", execute, validate, verifyOutput,
                            smoke, deadline);
                    {
                        auto a = map(input, co::Access::Read), b = map(output, co::Access::Write);
                        auto mappedRun = [&]() {
                            take(plan.run(a.read, b.write));
                            return std::uint64_t{1};
                        };
                        auto checkMapped = [&]() {
                            poison(b.write);
                            consumed = mappedRun();
                            verify(b.read);
                        };
                        measure(fields + ",\"scope\":\"mapped_run\"", mappedRun, checkMapped,
                                [&](std::uint64_t) { verify(b.read); }, smoke, deadline);
                        b.finish();
                        a.finish();
                    }
                    auto mapping = [&]() {
                        auto a = map(input, co::Access::Read), b = map(output, co::Access::Write);
                        b.finish();
                        a.finish();
                        return std::uint64_t{1};
                    };
                    measure(fields + ",\"scope\":\"map_finish\"", mapping, validate, verifyOutput,
                            smoke, deadline);
                }
            }
        }
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::string kind = "va", heapPath = "/dev/dma_heap/system";
        bool smoke = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--smoke")
                smoke = true;
            else if (a == "--memory" && i + 1 < argc)
                kind = argv[++i];
            else if (a == "--heap" && i + 1 < argc)
                heapPath = argv[++i];
            else
                throw std::runtime_error("usage: simd_resource_benchmark [--smoke] [--memory "
                                         "va|dma|ahb|ahb-rgba] [--heap path]");
        }
        check(kind == "va" || kind == "dma" || kind == "ahb" || kind == "ahb-rgba");
        co::FileDescriptor heap;
        if (kind == "dma") {
#if defined(__linux__)
            heap = co::FileDescriptor{open(heapPath.c_str(), O_RDONLY | O_CLOEXEC)};
            if (heap.get() < 0) {
                std::cerr << "DMA heap unavailable: " << std::strerror(errno) << '\n';
                return 77;
            }
#else
            return 77;
#endif
        }
#ifndef __ANDROID__
        if (kind == "ahb" || kind == "ahb-rgba")
            return 77;
#endif
        std::cout << std::setprecision(12)
                  << "{\"type\":\"environment\",\"schema\":1,\"compiler\":\"" << __VERSION__
                  << "\",\"build\":\"" << LUTILS_BENCH_BUILD
                  << "\",\"threads\":1,\"seed\":93721,\"clock\":\"steady_clock\",\"smoke\":"
                  << (smoke ? "true" : "false") << "}\n";
        run(kind, heap.get(), smoke, Clock::now() + std::chrono::seconds(45));
        std::cout << "{\"type\":\"complete\"}\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
