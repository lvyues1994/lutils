#include "CompositionKernel.hpp"
#include "CompositionNative.hpp"
namespace composition {
__attribute__((noinline, aligned(64))) std::uint64_t sse2(Operation op, Data const &data) {
    return run<s::abi::sse2>(op, data);
}
__attribute__((noinline, aligned(64))) std::uint64_t nativeSse2(Operation op, Data const &data) {
    return composition::nativeRun(op, data);
}
} // namespace composition
