#include <atomic_ops.hpp>
#include <cancellation.hpp>
#include <histogram.hpp>
#include <global_counter.hpp>
#include <iostream>
#include <reduction.hpp>
#ifdef LUTILS_TEST_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif

using namespace lutils::compute;
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
        throw std::runtime_error("workgroup mismatch");
}
int main(int argc, char **argv) {
    try {
        bool gpu = argc > 1;
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
                throw std::invalid_argument("unknown workgroup test option");
        }
        if (gpu)
            device = take(createVulkanDevice(options));
#else
        (void)argv;
#endif
        {
            Backend backend{std::move(device)};
            BufferResource<Word> output{8};
            take(backend.uploadBuffer(&output));
            group_test::Reduction reduction;
            reduction.output.attach(&output);
            for (int repeat = 0; repeat < 3; ++repeat) {
                take(take(backend.execute(reduction, {16, 4, 1}))->wait());
                take(backend.downloadBuffer(&output));
                for (int group = 0; group < 8; ++group) {
                    Word sum = 0;
                    for (Word y = 0; y < 2; ++y)
                        for (Word x = 0; x < 4; ++x)
                            sum += static_cast<Word>(group % 4) * 4 + x +
                                   (static_cast<Word>(group / 4) * 2 + y) * 16;
                    check(output[group] == sum);
                }
            }
            check(!backend.execute(reduction, {15, 4, 1}));
            BufferResource<Word> bins{4};
            take(backend.uploadBuffer(&bins));
            group_test::Histogram histogram;
            histogram.output.attach(&bins);
            take(take(backend.execute(histogram, {256, 1, 1}))->wait());
            take(backend.downloadBuffer(&bins));
            for (int i = 0; i < 4; ++i)
                check(bins[i] == 64);
            BufferResource<int> value{1}, old{9};
            value[0] = INT32_MAX;
            take(backend.uploadBuffer(&value));
            take(backend.uploadBuffer(&old));
            group_test::AtomicOps atomic;
            atomic.values.attach(&value);
            atomic.old.attach(&old);
            take(take(backend.execute(atomic, {1, 1, 1}))->wait());
            take(backend.downloadBuffer(&value));
            take(backend.downloadBuffer(&old));
            int expected[]{INT32_MAX, INT32_MIN, INT32_MIN, 12, 8, 11, 9, 20, 30};
            for (int i = 0; i < 9; ++i)
                check(old[i] == expected[i]);
            check(value[0] == 30);
            BufferResource<Word> counter{1};
            take(backend.uploadBuffer(&counter));
            group_test::GlobalCounter global;
            global.output.attach(&counter);
            take(take(backend.execute(global, {6001, 1, 1}))->wait());
            take(backend.downloadBuffer(&counter));
            check(counter[0] == 6001);
            if (!gpu) {
                BufferResource<Word> tooSmall{1}, enough{8};
                take(backend.uploadBuffer(&tooSmall));
                take(backend.uploadBuffer(&enough));
                group_test::Cancellation failure;
                failure.output.attach(&tooSmall);
                auto result = backend.execute(failure, {8, 1, 1});
                check(!result && result.error().code == lutils::ErrorCode::Bounds);
                failure.output.attach(&enough);
                take(take(backend.execute(failure, {8, 1, 1}))->wait());
            }
            std::cout << "shared memory, barriers, atomics and cancellation passed\n";
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
