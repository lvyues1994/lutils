#include <array>
#include <iostream>
#include <lutils/compute/Runtime.hpp>
#include <lutils/compute/Workgroup.hpp>
#include <stdexcept>
using namespace lutils::compute;
void check(bool value) {
    if (!value)
        throw std::runtime_error("CPU execution mismatch");
}
int main() {
    try {
        auto result = createCpuExecution({4, 1});
        check(bool(result));
        auto executor = std::move(result).value();
        std::vector<Word> output(10003);
        for (int repeat = 0; repeat < 8; ++repeat) {
            executor->parallelFor(output.size(), [&](std::size_t begin, std::size_t end) {
                for (auto i = begin; i < end; ++i)
                    ++output[i];
            });
            bool threw = false;
            try {
                executor->parallelFor(output.size(), [&](std::size_t begin, std::size_t end) {
                    if (begin <= 5000 && end > 5000)
                        throw std::out_of_range("injected worker failure");
                });
            } catch (std::out_of_range const &) {
                threw = true;
            }
            check(threw);
        }
        for (auto value : output)
            check(value == 8);
        Word counter = 0;
        executor->parallelFor(20003, [&](std::size_t begin, std::size_t end) {
            for (auto i = begin; i < end; ++i)
                kernel::atomicAdd(counter, 1u);
        });
        check(counter == 20003);
        std::array<Word, 8> shared{};
        for (std::size_t failure = 0; failure < 8; ++failure) {
            bool threw = false;
            try {
                executor->workgroup(8, [&](std::size_t lane) {
                    if (lane == failure)
                        throw std::out_of_range("injected lane failure");
                    kernel::barrier();
                });
            } catch (std::out_of_range const &) {
                threw = true;
            }
            check(threw);
            executor->workgroup(8, [&](std::size_t lane) {
                for (Word phase = 0; phase < 20; ++phase) {
                    shared[lane] = phase;
                    kernel::barrier();
                    for (auto value : shared)
                        check(value == phase);
                    kernel::barrier();
                }
            });
        }
        check(!createCpuExecution({257, 1}));
        std::cout << "parallel coverage, contended atomics, cancellation and recovery passed\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
