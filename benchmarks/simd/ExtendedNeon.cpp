#include "ExtendedKernel.hpp"
namespace extended {
std::uint64_t neon(Operation op, Data const &data) { return execute<s::abi::neon>(op, data); }
} // namespace extended
