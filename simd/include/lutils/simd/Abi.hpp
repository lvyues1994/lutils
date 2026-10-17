#pragma once
#include <cstddef>

namespace lutils::simd::abi {
struct scalar {
    static constexpr std::size_t bytes = 0;
};
struct sse2 {
    static constexpr std::size_t bytes = 16;
};
struct avx2 {
    static constexpr std::size_t bytes = 32;
};
struct neon {
    static constexpr std::size_t bytes = 16;
};
} // namespace lutils::simd::abi
