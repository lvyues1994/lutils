#pragma once
#include <cstddef>
#include <cstdint>
namespace micro {
enum class Operation { Widen, Narrow, Convert, Filter, Sum };
struct Data {
    std::uint8_t const *bytes;
    std::uint16_t const *words;
    std::int32_t const *integers;
    std::uint16_t *wide;
    std::uint8_t *narrow;
    float *floats;
    std::int32_t *filtered;
    std::size_t size;
};
using Function = std::uint64_t(Operation, Data const &);
Function scalar, sse2, avx2, neon;
} // namespace micro
