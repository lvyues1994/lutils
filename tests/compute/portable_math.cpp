#include "portable_math.hpp"
#include "integer_sqrt.hpp"
#include <iostream>
#include <stdexcept>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif
using namespace lutils::compute;
using namespace lutils::compute::kernel;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
int main(int argc, char **) {
    try {
        auto device = take(createCpuDevice());
#ifdef LUTILS_TEST_VULKAN
        if (argc > 1)
            device = take(createVulkanDevice({}));
#else
        (void)argc;
#endif
        Backend backend{std::move(device)};
        BufferResource<float> input{8}, output{8};
        for (int i = 0; i < 8; ++i)
            input[i] = static_cast<float>(i) + 0.25f;
        take(backend.uploadBuffer(&input));
        take(backend.uploadBuffer(&output));
        portable_math::Math kernel;
        kernel.input.attach(&input);
        kernel.output.attach(&output);
        take(take(backend.execute(kernel, {8, 1, 1}))->wait());
        take(backend.downloadBuffer(&output));
        for (int i = 0; i < 8; ++i) {
            float x = input[i];
            float expected = 2.0f * std::sqrt(x) + x + 9.0f + x + x * x + std::floor(x) +
                             std::ceil(x) + std::sin(x) + std::cos(x);
            if (std::abs(output[i] - expected) > 0.00002f)
                throw std::runtime_error("portable scalar math mismatch");
        }
        // C++ std::sqrt(int) returns double. Verify its generated CPU path unconditionally.
        Backend cpu{take(createCpuDevice())};
        BufferResource<double> result{1};
        take(cpu.uploadBuffer(&result));
        portable_math::IntegerSqrt integer;
        integer.output.attach(&result);
        take(take(cpu.execute(integer, {1, 1, 1}))->wait());
        take(cpu.downloadBuffer(&result));
        if (std::abs(result[0] - std::sqrt(2.0)) > 1e-14)
            throw std::runtime_error("integer sqrt lost double precision");
        std::cout << "portable math passed: " << backend.device().info().name << '\n';
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
