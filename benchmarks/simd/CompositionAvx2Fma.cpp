#include "CompositionKernel.hpp"
#include "CompositionNative.hpp"
namespace composition {
__attribute__((noinline, aligned(64))) std::uint64_t avx2Fma(Operation op, Data const &data) {
    return run<s::abi::avx2>(op, data);
}
__attribute__((noinline, aligned(64))) std::uint64_t nativeAvx2Fma(Operation op, Data const &data) {
    return composition::nativeRun(op, data);
}
} // namespace composition
