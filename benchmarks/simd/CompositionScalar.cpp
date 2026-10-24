#include "CompositionKernel.hpp"
namespace composition {
__attribute__((noinline, aligned(64))) std::uint64_t scalar(Operation op, Data const &data) {
    return run<s::abi::scalar>(op, data);
}
} // namespace composition
