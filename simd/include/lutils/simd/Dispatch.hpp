#pragma once
#include <initializer_list>
#include <optional>

namespace lutils::simd {
enum class Target { Automatic, Scalar, Sse2, Avx2, Neon };
struct CpuFeatures {
    bool sse2 = false;
    bool avx2 = false;
    bool neon = false;
    bool fma = false;
};
CpuFeatures cpu_features() noexcept;
bool supported(Target target) noexcept;
char const *target_name(Target target) noexcept;

template <class Signature> struct DispatchTable;
template <class R, class... Args> struct DispatchTable<R(Args...)> {
    using Function = R (*)(Args...);
    Function scalar = nullptr;
    Function sse2 = nullptr;
    Function avx2 = nullptr;
    Function neon = nullptr;
    // Optional specialization, compiled separately with AVX2 and FMA enabled.
    Function avx2Fma = nullptr;
};
template <class Signature> struct SelectedFunction {
    Target target;
    typename DispatchTable<Signature>::Function function;
};
template <class Signature>
std::optional<SelectedFunction<Signature>> resolve(DispatchTable<Signature> const &table,
                                                   Target requested = Target::Automatic) noexcept {
    // Selection is per operation: the CPU may support implementations absent from this table.
    for (auto target : {Target::Avx2, Target::Neon, Target::Sse2, Target::Scalar}) {
        if (requested != Target::Automatic && requested != target)
            continue;
        if (!supported(target))
            continue;
        typename DispatchTable<Signature>::Function function = nullptr;
        switch (target) {
        case Target::Scalar:
            function = table.scalar;
            break;
        case Target::Sse2:
            function = table.sse2;
            break;
        case Target::Avx2:
            function = table.avx2Fma && cpu_features().fma ? table.avx2Fma : table.avx2;
            break;
        case Target::Neon:
            function = table.neon;
            break;
        case Target::Automatic:
            break;
        }
        if (function)
            return SelectedFunction<Signature>{target, function};
    }
    return std::nullopt;
}
} // namespace lutils::simd
