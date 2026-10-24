#include <array>
#include <lutils/simd.hpp>
#include <lutils/simd/Dispatch.hpp>
#include <stdexcept>

namespace {
namespace s = lutils::simd;
void verify(bool value) {
    if (!value)
        throw std::runtime_error("installed SIMD API contract failed");
}
constexpr std::size_t backwards(std::size_t i) { return 6 - i; }
int baseline() { return 1; }
int specialized() { return 2; }
} // namespace

template <class A> void verifyVectors() {
    using V = s::Vec<std::int32_t, 7, A>;
    static_assert(s::rebind_t<double, V>::size == 7);
    std::array<std::int32_t, 9> source{1, 2, 3, 4, 5, 6, 7, 8, 9}, output{};
    V value;
    s::load(source.data(), value);
    auto reversed = s::permute<7, backwards>(value);
    auto parts = s::split<3>(reversed);
    auto joined = s::concat(parts.low, parts.high);
    auto middle = s::extract<2, 3>(joined);
    verify(middle.lane(0) == 5 && middle.lane(2) == 3);
    verify(s::try_store(s::MemoryView<std::int32_t>{output.data(), 9}, joined, 1));
    verify(output[0] == 0 && output[1] == 7 && output[7] == 1 && output[8] == 0);
    auto view = s::MemoryView<std::int32_t const>{source.data(), 9};
    verify(!s::try_load<V>(view, 3));
    auto tail = s::load_partial<V>(view, 8);
    verify(tail.count == 1 && s::count(tail.valid) == 1 && tail.value.lane(0) == 9);
}
void verifySimdConsumer() {
    verifyVectors<s::abi::scalar>();
#if defined(__SSE2__)
    verifyVectors<s::abi::sse2>();
#elif defined(__aarch64__)
    verifyVectors<s::abi::neon>();
#endif
    s::DispatchTable<int()> table;
    table.scalar = baseline;
    table.avx2Fma = specialized; // Test routing without emitting higher-ISA instructions here.
    auto selected = s::resolve(table);
    auto features = s::cpu_features();
    verify(selected && selected->function() == (features.avx2 && features.fma ? 2 : 1));
}
