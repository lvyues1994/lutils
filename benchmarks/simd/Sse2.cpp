#include "Kernel.hpp"
namespace micro {
std::uint64_t sse2(Operation op, Data const &data) { return execute<s::abi::sse2>(op, data); }
} // namespace micro
