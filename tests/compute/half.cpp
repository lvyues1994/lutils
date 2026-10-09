#include <half_arithmetic.hpp>
#include <half_shared.hpp>
#include <iostream>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif
using namespace lutils::compute;
using namespace lutils::compute::kernel;
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
void take(lutils::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
void check(bool v) {
    if (!v)
        throw std::runtime_error("FP16 result mismatch");
}
int main(int argc, char **argv) {
    try {
        auto source = KernelTraits<half_test::Arithmetic>::source();
        Word halfType = 0;
        bool halfAdd = false, halfMultiply = false;
        for (std::size_t i = 5; i < source.spirv.size(); i += source.spirv[i] >> 16) {
            auto op = source.spirv[i] & 0xffffu;
            if (op == 22u && source.spirv[i + 2] == 16u)
                halfType = source.spirv[i + 1];
            if (halfType && (source.spirv[i] >> 16) >= 2 && source.spirv[i + 1] == halfType) {
                halfAdd |= op == 129u;
                halfMultiply |= op == 133u;
            }
        }
        check(halfType && halfAdd && halfMultiply);
        auto device = take(createCpuDevice());
#ifdef LUTILS_TEST_VULKAN
        VulkanOptions options;
        options.validationReport = std::make_shared<ValidationReport>();
        for (int i = 2; i < argc; ++i) {
            std::string arg{argv[i]};
            if (arg == "--validation")
                options.enableValidation = true;
            else if (arg == "--require-hardware")
                options.requireHardware = true;
            else
                throw std::invalid_argument("unknown FP16 test option");
        }
#else
        (void)argv;
#endif
        if (argc > 1) {
#ifdef LUTILS_TEST_VULKAN
            VulkanOptions disabled = options;
            disabled.enableFloat16 = false;
            auto missing = take(createVulkanDevice(disabled));
            auto rejected = missing->createKernel(KernelTraits<half_test::Arithmetic>::source());
            check(!rejected && rejected.error().code == lutils::ErrorCode::Unsupported);
            device = take(createVulkanDevice(options));
            auto c = device->info().capabilities;
            if (!c.float16 || !c.storageBuffer16 || !c.pushConstant16) {
                std::cout << "SKIP: device lacks required FP16 features\n";
                return 77;
            }
#endif
        }
        {
            Backend backend{std::move(device)};
            BufferResource<half> input{3}, output{3};
            BufferResource<half_test::Nested> records{3};
            for (int i = 0; i < 3; ++i)
                input[i] = half(i + 1);
            take(backend.uploadBuffer(&input));
            take(backend.uploadBuffer(&output));
            take(backend.uploadBuffer(&records));
            half_test::Arithmetic k;
            k.input.attach(&input);
            k.output.attach(&output);
            k.records.attach(&records);
            k.scale = half(2);
            k.params = half_test::Mixed{half(4), f16vec3{half(1), half(2), half(3)}, half(7), 0.0f};
            auto handle = take(backend.resolve(k.input));
            check(handle->byteCount() == 6 && handle->wordCount() == 2);
            static_assert(StorageCodec<half>::bytes == 2 && StorageCodec<f16vec3>::bytes == 6);
            static_assert(StorageCodec<half_test::Mixed>::bytes == 24);
            static_assert(StorageCodec<half_test::Nested>::bytes == 32);
            take(take(backend.execute(k, {3, 1, 1}))->wait());
            take(backend.downloadBuffer(&output));
            take(backend.downloadBuffer(&records));
            for (int i = 0; i < 3; ++i) {
                check(output[i] == half((i + 1) * 2.0f + 0.5f));
                check(records[i].value.a == half(i + 5));
                check(records[i].value.b == f16vec3{half(2), half(4), half(6)});
                check(records[i].value.c == half(7) &&
                      records[i].value.d == static_cast<float>(i + 1));
                check(records[i].tail == std::array<half, 3>{half(2), half(2), half(4)});
            }
            BufferResource<half> sharedOutput{8};
            take(backend.uploadBuffer(&sharedOutput));
            half_test::Shared shared;
            shared.output.attach(&sharedOutput);
            take(take(backend.execute(shared, {8, 1, 1}))->wait());
            take(backend.downloadBuffer(&sharedOutput));
            for (int i = 0; i < 8; ++i)
                check(sharedOutput[i] == half(3 - i % 4));
            std::cout << "FP16 arithmetic, odd buffers, nested layouts, uniforms and shared "
                         "storage passed\n";
        }
#ifdef LUTILS_TEST_VULKAN
        check(options.validationReport->errors.load() == 0);
        check(options.validationReport->warnings.load() == 0);
#endif
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
