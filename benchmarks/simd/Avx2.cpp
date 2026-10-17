#include "Kernel.hpp"
namespace micro {
std::uint64_t avx2(Operation op, Data const &data) { return execute<s::abi::avx2>(op, data); }
} // namespace micro
