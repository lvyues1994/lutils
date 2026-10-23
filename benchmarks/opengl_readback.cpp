#include "camera_rays.hpp"
#include "sphere_tracer.hpp"
#include "visualize_rays.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <lutils/compute/OpenGl.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace co = lutils::compute;
namespace k = co::kernel;
using Clock = std::chrono::steady_clock;
using Image = co::BufferResource<k::cpu::RGBA32F, k::Dim::D2>;
using Output = co::BufferResource<k::cpu::BGRA8, k::Dim::D2>;
constexpr int warmups = 10, sampleCount = 50;

template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
struct Prepared {
    co::BufferResource<test_pdf::Sphere> spheres;
    Image rays, colors;
    Output output;
    co::CommandList commands;
};
Prepared prepare(co::Backend &backend, k::ivec2 size) {
    Prepared result{
        co::BufferResource<test_pdf::Sphere>{2}, Image{size}, Image{size}, Output{size}, {}};
    result.spheres[0] = {{0.0f, 0.0f, -3.0f, 0.8f}, {0.9f, 0.2f, 0.1f, 1.0f}};
    result.spheres[1] = {{1.0f, 0.3f, -4.0f, 0.5f}, {0.2f, 0.9f, 0.1f, 1.0f}};
    take(backend.uploadBuffer(&result.spheres));
    take(backend.uploadImage<k::gpu::RGBA32F>(&result.rays));
    take(backend.uploadImage<k::gpu::RGBA32F>(&result.colors));
    take(backend.uploadImage<k::gpu::RGBA8>(&result.output));
    test_pdf::CameraRays camera;
    camera.rays.attach(&result.rays);
    camera.aspect = static_cast<float>(size.x) / static_cast<float>(size.y);
    test_pdf::SphereTracer tracer;
    tracer.rays.attach(&result.rays);
    tracer.colors.attach(&result.colors);
    tracer.spheres.attach(&result.spheres);
    tracer.count = 2;
    test_pdf::VisualizeRays visualize;
    visualize.colors.attach(&result.colors);
    visualize.output.attach(&result.output);
    co::Extent3 extent{static_cast<co::Word>(size.x), static_cast<co::Word>(size.y), 1};
    take(backend.record(result.commands, camera, extent));
    take(backend.record(result.commands, tracer, extent));
    take(backend.record(result.commands, visualize, extent));
    return result;
}
void frame(co::Backend &backend, Prepared &state, bool readback) {
    take(take(backend.device().submit(state.commands))->wait());
    if (readback)
        take(backend.downloadImage<k::gpu::RGBA8>(&state.output));
}
void save(Output const &output, std::string const &path) {
    auto size = output.extent();
    std::ofstream file{path, std::ios::binary};
    file << "P6\n" << size.x << ' ' << size.y << "\n255\n";
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x) {
            auto const &p = output[{x, y}];
            char rgb[]{static_cast<char>(p.data[2]), static_cast<char>(p.data[1]),
                       static_cast<char>(p.data[0])};
            file.write(rgb, 3);
        }
    file.close();
    if (!file)
        throw std::runtime_error("cannot write PPM: " + path);
}
void verify(Output const &actual, k::ivec2 size) {
    co::CpuOptions options;
    options.workerCount = 2;
    co::Backend reference{take(co::createCpuDevice(options))};
    auto state = prepare(reference, size);
    frame(reference, state, true);
    int maximum = 0;
    std::size_t different = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        for (std::size_t c = 0; c < 4; ++c) {
            int error = std::abs(static_cast<int>(actual.data()[i].data[c]) -
                                 static_cast<int>(state.output.data()[i].data[c]));
            maximum = std::max(maximum, error);
            different += static_cast<std::size_t>(error != 0);
        }
    std::cout << "verification " << size.x << 'x' << size.y << " max_error=" << maximum
              << " differing_BGRA_channels=" << different << '\n';
    if (maximum > 1)
        throw std::runtime_error("render differs from CPU reference by more than 1/255");
}
void summarize(std::ofstream &file, k::ivec2 size, char const *name, std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    auto median = (samples[24] + samples[25]) / 2.0;
    auto p95 = samples[47]; // nearest-rank percentile for 50 samples
    file << size.x << ',' << size.y << ',' << name << ',' << warmups << ',' << sampleCount << ','
         << median << ',' << p95 << ',' << 1000.0 / median << '\n';
}
void benchmark(k::ivec2 size, std::string const &directory, std::ofstream &samplesFile,
               std::ofstream &summaryFile) {
    co::OpenGlOptions options;
    options.target = co::GlslTarget::OpenGl43; // Desktop GL, EGL device 0, headless.
    options.deviceIndex = 0;
    options.requireHardware = true;
    co::Backend backend{take(co::createOpenGlDevice(options))};
    auto info = backend.device().info();
    if (info.software)
        throw std::runtime_error("benchmark requires hardware OpenGL");
    std::cout << "renderer=" << info.name << " software=" << info.software << '\n';
    auto state = prepare(backend, size);
    for (int i = 0; i < warmups; ++i)
        frame(backend, state, true);
    verify(state.output, size);
    // Restore the device/driver path after the independent CPU correctness check.
    for (int i = 0; i < warmups; ++i)
        frame(backend, state, true);
    std::vector<double> resident, endToEnd;
    resident.reserve(sampleCount);
    endToEnd.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        for (int pass = 0; pass < 2; ++pass) {
            bool readback = ((i + pass) % 2) != 0;
            auto start = Clock::now();
            frame(backend, state, readback);
            auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            (readback ? endToEnd : resident).push_back(ms);
            samplesFile << size.x << ',' << size.y << ',' << i << ','
                        << (readback ? "with_readback" : "resident") << ',' << ms << '\n';
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    take(backend.downloadImage<k::gpu::RGBA8>(&state.output));
    verify(state.output, size);
    save(state.output,
         directory + "/opengl-" + std::to_string(size.x) + "x" + std::to_string(size.y) + ".ppm");
    summarize(summaryFile, size, "resident", resident);
    summarize(summaryFile, size, "with_readback", endToEnd);
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument("usage: opengl_readback_benchmark output-directory");
        std::filesystem::create_directories(argv[1]);
        std::ofstream samples{std::string{argv[1]} + "/samples.csv"};
        std::ofstream summary{std::string{argv[1]} + "/summary.csv"};
        if (!samples || !summary)
            throw std::runtime_error("cannot open benchmark reports");
        samples << std::setprecision(9) << "width,height,pair,mode,milliseconds\n";
        summary << std::setprecision(9)
                << "width,height,mode,warmup_frames_after_verification,samples,p50_ms,p95_ms,"
                   "equivalent_fps_at_p50\n";
        for (auto size : {k::ivec2{960, 720}, k::ivec2{1920, 1080}, k::ivec2{3840, 2160}})
            benchmark(size, argv[1], samples, summary);
        if (!samples || !summary)
            throw std::runtime_error("cannot write benchmark reports");
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
