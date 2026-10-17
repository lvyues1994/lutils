#include "Kernel.hpp"
namespace micro {
std::uint64_t scalar(Operation op, Data const &data) { return execute<s::abi::scalar>(op, data); }
} // namespace micro
