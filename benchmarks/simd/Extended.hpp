#pragma once
#include <cstddef>
#include <cstdint>
namespace extended {
enum class Operation { Signed, Unsigned, Filter8, Filter16, Filter32, Filter64 };
struct Data {
    void const *input;
    void *output;
    std::size_t size;
    unsigned rounding;
    bool allowOverwrite = false;
};
using Function = std::uint64_t(Operation, Data const &);
Function scalar, sse2, avx2, neon;
} // namespace extended
