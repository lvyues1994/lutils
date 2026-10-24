#pragma once
#include <cstddef>
#include <type_traits>

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
template <std::size_t N, class Native> struct fixed {
    static_assert(N > 0 && N <= 64, "fixed SIMD requires 1..64 lanes");
    static_assert(std::is_same_v<Native, scalar> || std::is_same_v<Native, sse2> ||
                      std::is_same_v<Native, avx2> || std::is_same_v<Native, neon>,
                  "fixed SIMD requires a native ABI");
};
} // namespace lutils::simd::abi
