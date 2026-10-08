#pragma once
#include <lutils/compute/Shader.hpp>
namespace test_pdf {
using namespace lutils::compute::kernel;
struct LUTILS_KERNEL FloatAdder {
    static constexpr char fileLocation[] = "typed_add";
    uvec3 local_size{256, 1, 1};
    BufferBinding<float, 0> A;
    BufferBinding<float, 1> B;
    BufferBinding<float, 2> C;
    Uniform<float, 0> scale{1.0f};
    void main() {
        uint i = gl_GlobalInvocationID.x;
        C[i] = (A[i] + B[i]) * scale;
    }
};
} // namespace test_pdf

namespace test_pdf {
struct Sphere {
    vec4 locR;
    vec4 color;
};
struct LUTILS_KERNEL CameraRays {
    static constexpr char fileLocation[] = "camera_rays";
    uvec3 local_size{8, 8, 1};
    ImageBinding<gpu::RGBA32F, Dim::D2, cpu::RGBA32F, 0> rays;
    Uniform<float, 0> aspect{1.0f};
    void main() {
        ivec2 p{gl_GlobalInvocationID["xy"_sw]};
        ivec2 size = imageSize(rays);
        vec2 uv = (vec2{p} + vec2{0.5f}) / vec2{size} * 2.0f - vec2{1.0f};
        vec3 direction = normalize(vec3{uv.x * aspect, uv.y, -1.5f});
        imageStore(rays, p, vec4{direction, 0.0f});
    }
};
struct LUTILS_KERNEL SphereTracer {
    static constexpr char fileLocation[] = "sphere_tracer";
    uvec3 local_size{8, 8, 1};
    BufferBinding<Sphere, 1> spheres;
    ImageBinding<gpu::RGBA32F, Dim::D2, cpu::RGBA32F, 0> rays;
    ImageBinding<gpu::RGBA32F, Dim::D2, cpu::RGBA32F, 1> colors;
    Uniform<uint, 1> count{0};
    Uniform<vec3, 2> eye{vec3{0.0f}};
    float hit(vec3 direction, Sphere sphere) {
        vec3 center{sphere.locR["xyz"_sw]};
        vec3 oc = eye - center;
        float b = dot(oc, direction);
        float c = dot(oc, oc) - sphere.locR.w * sphere.locR.w;
        float disc = b * b - c;
        if (disc < 0.0f)
            return -1.0f;
        return -b - sqrt(disc);
    }
    void main() {
        ivec2 p{gl_GlobalInvocationID["xy"_sw]};
        vec3 direction{imageLoad(rays, p)["xyz"_sw]};
        float closest = 10000.0f;
        vec4 color{0.05f, 0.1f, 0.2f, 1.0f};
        for (uint i = 0; i < count; ++i) {
            Sphere sphere = spheres[i];
            float distance = hit(direction, sphere);
            if (distance > 0.0f && distance < closest) {
                closest = distance;
                vec3 center{sphere.locR["xyz"_sw]};
                vec3 normal = normalize(vec3{eye} + direction * distance - center);
                float light = max(dot(normal, normalize(vec3{-1.0f, 1.0f, 1.0f})), 0.15f);
                color = vec4{vec3{sphere.color["xyz"_sw]} * light, 1.0f};
            }
        }
        imageStore(colors, p, color);
    }
};
struct LUTILS_KERNEL VisualizeRays {
    static constexpr char fileLocation[] = "visualize_rays";
    uvec3 local_size{8, 8, 1};
    ImageBinding<gpu::RGBA32F, Dim::D2, cpu::RGBA32F, 0> colors;
    ImageBinding<gpu::RGBA8, Dim::D2, cpu::BGRA8, 1> output;
    void main() {
        ivec2 p{gl_GlobalInvocationID["xy"_sw]};
        imageStore(output, p, clamp(imageLoad(colors, p), 0.0f, 1.0f));
    }
};
struct LUTILS_KERNEL GameOfLife {
    static constexpr char fileLocation[] = "game_of_life";
    uvec3 local_size{8, 8, 1};
    ImageBinding<gpu::R8UI, Dim::D2, cpu::R8UI, 0> current;
    ImageBinding<gpu::R8UI, Dim::D2, cpu::R8UI, 1> next;
    std::array<ivec2, 8> neighbors{
        {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
    void main() {
        ivec2 p{gl_GlobalInvocationID["xy"_sw]};
        ivec2 size = imageSize(current);
        if (p.x == 0 || p.y == 0 || p.x == size.x - 1 || p.y == size.y - 1) {
            imageStore(next, p, uvec4{0u});
            return;
        }
        uint sum = 0;
        for (uint i = 0; i < neighbors.size(); ++i) {
            sum += uint(imageLoad(current, p + neighbors[i]).x != 0u);
        }
        bool alive = imageLoad(current, p).x != 0u;
        uint value = uint(sum == 3u || (alive && sum == 2u));
        imageStore(next, p, uvec4{value, 0u, 0u, 1u});
    }
};
struct LUTILS_KERNEL Volume {
    static constexpr char fileLocation[] = "volume";
    uvec3 local_size{2, 2, 2};
    ImageBinding<gpu::R32UI, Dim::D3, cpu::R16UI, 7> image;
    void main() {
        ivec3 p{gl_GlobalInvocationID};
        uint value = gl_GlobalInvocationID.x + 10u * gl_GlobalInvocationID.y +
                     100u * gl_GlobalInvocationID.z;
        imageStore(image, p, uvec4{value, 0u, 0u, 1u});
    }
};
struct LUTILS_KERNEL Line {
    static constexpr char fileLocation[] = "line";
    uvec3 local_size{4, 1, 1};
    ImageBinding<gpu::RG16I, Dim::D1, cpu::RG16I, 3> image;
    void main() {
        int p = int(gl_GlobalInvocationID.x);
        imageStore(image, p, ivec4{p - 4, p + 2, 0, 1});
    }
};
} // namespace test_pdf
