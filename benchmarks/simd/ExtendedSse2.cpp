#include "ExtendedKernel.hpp"
namespace extended {
std::uint64_t sse2(Operation op, Data const &data) { return execute<s::abi::sse2>(op, data); }
} // namespace extended
