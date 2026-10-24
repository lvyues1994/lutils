#pragma once
#include <cstddef>
#include <cstdint>

namespace composition {
enum class Operation { Min, Max, SelectInt, SelectFloat, Zip, Unzip, Reduce, Fma };
struct Data {
    void const *a;
    void const *b;
    void const *c;
    void const *d;
    void *output;
    std::size_t size;
};
using Function = std::uint64_t(Operation, Data const &);
Function scalar, sse2, avx2, avx2Fma, neon;
Function nativeSse2, nativeAvx2, nativeAvx2Fma, nativeNeon;
} // namespace composition
