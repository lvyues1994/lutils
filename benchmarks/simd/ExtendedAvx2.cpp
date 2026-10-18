#include "ExtendedKernel.hpp"
namespace extended {
std::uint64_t avx2(Operation op, Data const &data) { return execute<s::abi::avx2>(op, data); }
} // namespace extended
