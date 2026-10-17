#include "Kernel.hpp"
namespace micro {
std::uint64_t neon(Operation op, Data const &data) { return execute<s::abi::neon>(op, data); }
} // namespace micro
