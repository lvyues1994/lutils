#include "CompositionKernel.hpp"
#include "CompositionNative.hpp"
namespace composition {
__attribute__((noinline, aligned(64))) std::uint64_t neon(Operation op, Data const &data) {
    return run<s::abi::neon>(op, data);
}
__attribute__((noinline, aligned(64))) std::uint64_t nativeNeon(Operation op, Data const &data) {
    return composition::nativeRun(op, data);
}
} // namespace composition
