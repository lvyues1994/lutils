#include <iostream>
#include <lutils/simd/Dispatch.hpp>
#include <lutils/simd/detail/Cpu.hpp>
#include <stdexcept>
#include <string_view>

void runScalar();
void runSse2();
void runAvx2();
void runAvx2Fma();
void runNeon();
namespace s = lutils::simd;
void check(bool value) {
    if (!value)
        throw std::runtime_error("dispatch mismatch");
}
int identity(int x) { return x; }
int fusedIdentity(int x) { return x + 1; }
void dispatch() {
    using s::detail::decode;
    constexpr auto state = (1u << 26) | (1u << 27) | (1u << 28);
    check(!decode({}).avx2);
    check(decode({0, 1u << 26, 0, 0}).sse2);
    for (unsigned bit : {26u, 27u, 28u}) {
        auto f = decode({(state | (1u << 12)) & ~(1u << bit), 0, 1u << 5, 6});
        check(!f.avx2 && !f.fma);
    }
    for (auto xcr0 : {0u, 2u, 4u})
        check(!decode({state, 0, 1u << 5, xcr0}).avx2);
    check(decode({state, 0, 1u << 5, 6}).avx2);
    check(!decode({state, 0, 1u << 5, 6}).fma);
    check(decode({state | (1u << 12), 0, 0, 6}).fma);
    check(!decode({state | (1u << 12), 0, 0, 6}).avx2);
    s::DispatchTable<int(int)> table;
    check(!s::resolve(table));
    table.scalar = identity;
    auto selected = s::resolve(table);
    check(selected && selected->target == s::Target::Scalar && selected->function(42) == 42);
    check(!s::resolve(table, s::Target::Avx2));
    table.avx2 = table.sse2 = table.neon = identity;
    selected = s::resolve(table);
    check(selected && s::supported(selected->target));
    for (auto target : {s::Target::Sse2, s::Target::Avx2, s::Target::Neon})
        check(bool(s::resolve(table, target)) == s::supported(target));
    table.avx2Fma = fusedIdentity;
    selected = s::resolve(table, s::Target::Avx2);
    if (selected)
        check(selected->function(42) == (s::cpu_features().fma ? 43 : 42));
    table.avx2 = nullptr;
    selected = s::resolve(table, s::Target::Avx2);
    check(bool(selected) == (s::cpu_features().avx2 && s::cpu_features().fma));
}
int main(int argc, char **argv) {
    try {
        std::string_view name = argc > 1 ? argv[1] : "dispatch";
        if (name == "dispatch") {
            dispatch();
            return 0;
        }
#if defined(LUTILS_SIMD_X86)
        if (name == "avx2_fma") {
            s::DispatchTable<void()> fused;
            fused.avx2Fma = runAvx2Fma;
            auto selected = s::resolve(fused, s::Target::Avx2);
            if (!selected)
                return 77;
            selected->function();
            std::cout << "avx2_fma contracts passed\n";
            return 0;
        }
#endif
        s::DispatchTable<void()> table;
        table.scalar = runScalar;
#if defined(LUTILS_SIMD_X86)
        table.sse2 = runSse2;
        table.avx2 = runAvx2;
#elif defined(LUTILS_SIMD_ARM64)
        table.neon = runNeon;
#endif
        for (auto target : {s::Target::Scalar, s::Target::Sse2, s::Target::Avx2, s::Target::Neon}) {
            if (name != s::target_name(target))
                continue;
            auto selected = s::resolve(table, target);
            if (!selected)
                return 77;
            selected->function();
            std::cout << name << " contracts passed\n";
            return 0;
        }
        throw std::runtime_error("unknown target");
    } catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
