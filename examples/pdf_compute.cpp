#include "camera_rays.hpp"
#include "game_of_life.hpp"
#include "sphere_tracer.hpp"
#include "typed_add.hpp"
#include "visualize_rays.hpp"
#include <fstream>
#include <iostream>
using namespace lutils::compute;
namespace k = lutils::compute::kernel;
template <class T> T value(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void value(lutils::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
int main(int argc, char **argv) {
    try {
        auto device = createCpuDevice();
        if (argc > 1 && std::string{argv[1]} == "gpu") {
#ifdef LUTILS_EXAMPLE_VULKAN
            device = createVulkanDevice();
#else
            throw std::runtime_error("build with LUTILS_ENABLE_VULKAN=ON");
#endif
        }
        Backend backend{value(std::move(device))};
        BufferResource<float> a{7}, b{7}, c{7};
        for (int i = 0; i < 7; ++i) {
            a[i] = float(i);
            b[i] = float(i) * 0.5f;
        }
        value(backend.uploadBuffer(&a));
        value(backend.uploadBuffer(&b));
        value(backend.uploadBuffer(&c));
        test_pdf::FloatAdder adder;
        adder.A.attach(&a);
        adder.B.attach(&b);
        adder.C.attach(&c);
        value(value(backend.execute(adder, {7, 1, 1}))->wait());
        value(backend.downloadBuffer(&c));
        std::cout << backend.device().info().name << "\nFloatAdder:";
        for (int i = 0; i < 7; ++i)
            std::cout << ' ' << c[i];
        std::cout << '\n';
        constexpr int w = 129, h = 97;
        BufferResource<test_pdf::Sphere> spheres{2};
        spheres[0] = {{0.0f, 0.0f, -3.0f, 0.8f}, {0.9f, 0.2f, 0.1f, 1.0f}};
        spheres[1] = {{1.0f, 0.3f, -4.0f, 0.5f}, {0.2f, 0.9f, 0.1f, 1.0f}};
        BufferResource<k::cpu::RGBA32F, k::Dim::D2> rays{{w, h}}, colors{{w, h}};
        BufferResource<k::cpu::BGRA8, k::Dim::D2> output{{w, h}};
        value(backend.uploadBuffer(&spheres));
        value(backend.uploadImage<k::gpu::RGBA32F>(&rays));
        value(backend.uploadImage<k::gpu::RGBA32F>(&colors));
        value(backend.uploadImage<k::gpu::RGBA8>(&output));
        test_pdf::CameraRays camera;
        camera.rays.attach(&rays);
        camera.aspect = float(w) / float(h);
        test_pdf::SphereTracer tracer;
        tracer.rays.attach(&rays);
        tracer.colors.attach(&colors);
        tracer.spheres.attach(&spheres);
        tracer.count = 2;
        test_pdf::VisualizeRays visualize;
        visualize.colors.attach(&colors);
        visualize.output.attach(&output);
        CommandList commands;
        value(backend.record(commands, camera, {w, h, 1}));
        value(backend.record(commands, tracer, {w, h, 1}));
        value(backend.record(commands, visualize, {w, h, 1}));
        value(value(backend.device().submit(commands))->wait());
        value(backend.downloadImage<k::gpu::RGBA8>(&output));
        if (argc > 2) {
            std::ofstream file{argv[2], std::ios::binary};
            file << "P6\n" << w << ' ' << h << "\n255\n";
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    auto const &p = output[{x, y}];
                    char rgb[]{static_cast<char>(p.data[2]), static_cast<char>(p.data[1]),
                               static_cast<char>(p.data[0])};
                    file.write(rgb, 3);
                }
            if (!file)
                throw std::runtime_error("cannot write PPM image");
        }
        BufferResource<k::cpu::R8UI, k::Dim::D2> current{{9, 9}}, next{{9, 9}};
        for (int x = 3; x <= 5; ++x)
            current[{x, 4}].set<k::Channel::R>(1);
        value(backend.uploadImage<k::gpu::R8UI>(&current));
        value(backend.uploadImage<k::gpu::R8UI>(&next));
        test_pdf::GameOfLife life;
        for (int step = 0; step < 4; ++step) {
            life.current.attach(&current);
            life.next.attach(&next);
            value(value(backend.execute(life, {9, 9, 1}))->wait());
            std::swap(current, next);
        }
        value(backend.downloadImage<k::gpu::R8UI>(&current));
        std::cout << "Raytracer: " << w << 'x' << h << "; Game of Life: 4 generations\n";
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x)
                std::cout << (current[{x, y}].get<k::Channel::R>() ? '#' : '.');
            std::cout << '\n';
        }
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
