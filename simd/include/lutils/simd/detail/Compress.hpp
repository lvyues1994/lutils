#pragma once
#include <lutils/simd/detail/Scalar.hpp>
namespace lutils::simd::detail {
template <class T, class A> struct CompressNative {
    static constexpr bool available = false;
};
struct CompressIndexes {
    std::uint8_t lanes[256][8]{};
    std::uint8_t bytes[16][16]{};
    std::uint8_t counts[256]{};
};
constexpr CompressIndexes makeCompressIndexes() noexcept {
    CompressIndexes result{};
    for (unsigned mask = 0; mask < 256; ++mask) {
        unsigned count = 0;
        for (unsigned i = 0; i < 8; ++i)
            if ((mask >> i) & 1u)
                result.lanes[mask][count++] = static_cast<std::uint8_t>(i);
        result.counts[mask] = static_cast<std::uint8_t>(count);
        while (count < 8)
            result.lanes[mask][count++] = 128;
    }
    for (unsigned mask = 0; mask < 16; ++mask) {
        unsigned count = 0;
        for (unsigned i = 0; i < 4; ++i)
            if ((mask >> i) & 1u)
                for (unsigned byte = 0; byte < 4; ++byte)
                    result.bytes[mask][count++] = static_cast<std::uint8_t>(4 * i + byte);
        while (count < 16)
            result.bytes[mask][count++] = 128;
    }
    return result;
}
inline constexpr auto compressIndexes = makeCompressIndexes();
} // namespace lutils::simd::detail
#if defined(__AVX2__)
#include <immintrin.h>
namespace lutils::simd::detail {
template <class T> struct CompressNative<T, abi::avx2> {
    static constexpr bool available = sizeof(T) == 4;
    static LUTILS_SIMD_INLINE void run(T *out, T const *in, std::uint64_t mask) noexcept {
        auto indexes =
            _mm_loadl_epi64(reinterpret_cast<__m128i const *>(compressIndexes.lanes[mask]));
        auto expanded = _mm256_cvtepu8_epi32(indexes);
        auto values = _mm256_permutevar8x32_epi32(
            _mm256_loadu_si256(reinterpret_cast<__m256i const *>(in)), expanded);
        auto valid = _mm256_cmpgt_epi32(_mm256_set1_epi32(8), expanded);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), _mm256_and_si256(values, valid));
    }
};
} // namespace lutils::simd::detail
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
namespace lutils::simd::detail {
template <class T> struct CompressNative<T, abi::neon> {
    static constexpr bool available = sizeof(T) == 4;
    static LUTILS_SIMD_INLINE void run(T *out, T const *in, std::uint64_t mask) noexcept {
        auto indexes = vld1q_u8(compressIndexes.bytes[mask]);
        auto values = vld1q_u8(reinterpret_cast<std::uint8_t const *>(in));
        vst1q_u8(reinterpret_cast<std::uint8_t *>(out), vqtbl1q_u8(values, indexes));
    }
};
} // namespace lutils::simd::detail
#endif
