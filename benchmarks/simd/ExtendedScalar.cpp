#include "ExtendedKernel.hpp"
namespace extended {
std::uint64_t scalar(Operation op, Data const &data) { return execute<s::abi::scalar>(op, data); }
} // namespace extended
