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
struct ByteCompressIndexes {
    std::uint8_t bytes[256][16]{};
    std::uint8_t words[256][16]{};
    std::uint8_t qwords[16][8]{};
    std::uint8_t left[17][16]{};
    std::uint8_t right[17][16]{};
};
constexpr ByteCompressIndexes makeByteCompressIndexes() noexcept {
    ByteCompressIndexes result{};
    for (unsigned mask = 0; mask < 256; ++mask) {
        unsigned n = 0;
        for (unsigned i = 0; i < 8; ++i)
            if ((mask >> i) & 1u) {
                result.bytes[mask][n] = static_cast<std::uint8_t>(i);
                result.words[mask][2 * n] = static_cast<std::uint8_t>(2 * i);
                result.words[mask][2 * n + 1] = static_cast<std::uint8_t>(2 * i + 1);
                ++n;
            }
        for (unsigned i = n; i < 16; ++i)
            result.bytes[mask][i] = 128;
        for (unsigned i = 2 * n; i < 16; ++i)
            result.words[mask][i] = 128;
    }
    for (unsigned mask = 0; mask < 16; ++mask) {
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i)
            if ((mask >> i) & 1u) {
                result.qwords[mask][n++] = static_cast<std::uint8_t>(2 * i);
                result.qwords[mask][n++] = static_cast<std::uint8_t>(2 * i + 1);
            }
        while (n < 8)
            result.qwords[mask][n++] = 128;
    }
    for (unsigned n = 0; n <= 16; ++n)
        for (unsigned i = 0; i < 16; ++i) {
            result.left[n][i] = i >= n ? static_cast<std::uint8_t>(i - n) : 128;
            result.right[n][i] = i < n ? static_cast<std::uint8_t>(16 - n + i) : 128;
        }
    return result;
}
inline constexpr auto byteCompressIndexes = makeByteCompressIndexes();
} // namespace lutils::simd::detail
#if defined(__AVX2__)
#include <immintrin.h>
namespace lutils::simd::detail {
template <class T> struct CompressNative<T, abi::avx2> {
    static constexpr bool available = true;
    static LUTILS_SIMD_INLINE __m128i compact128(__m128i value, unsigned mask) noexcept {
        if constexpr (sizeof(T) == 2)
            return _mm_shuffle_epi8(value, _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                               byteCompressIndexes.words[mask])));
        else {
            auto lo = _mm_shuffle_epi8(value, _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                                  byteCompressIndexes.bytes[mask & 255])));
            auto hi = _mm_shuffle_epi8(_mm_srli_si128(value, 8),
                                       _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                           byteCompressIndexes.bytes[mask >> 8])));
            auto offset = compressIndexes.counts[mask & 255];
            return _mm_or_si128(
                lo, _mm_shuffle_epi8(hi, _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                             byteCompressIndexes.left[offset]))));
        }
    }
    static LUTILS_SIMD_INLINE void run(T *out, T const *in, std::uint64_t mask) noexcept {
        if constexpr (sizeof(T) <= 2) {
            constexpr unsigned halfLanes = 16 / sizeof(T), halfMask = (1u << halfLanes) - 1;
            auto bits = static_cast<unsigned>(mask);
            auto all = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(in));
            auto lo = compact128(_mm256_castsi256_si128(all), bits & halfMask);
            auto hi = compact128(_mm256_extracti128_si256(all, 1), bits >> halfLanes);
            // The join needs the low-half length, not the total selected count.
            auto count = unsigned(compressIndexes.counts[bits & 255]);
            if constexpr (sizeof(T) == 1)
                count += compressIndexes.counts[(bits >> 8) & 255];
            auto offset = count * sizeof(T);
            auto first = _mm_or_si128(
                lo, _mm_shuffle_epi8(hi, _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                             byteCompressIndexes.left[offset]))));
            auto second = _mm_shuffle_epi8(hi, _mm_loadu_si128(reinterpret_cast<__m128i const *>(
                                                   byteCompressIndexes.right[offset])));
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(out),
                                _mm256_inserti128_si256(_mm256_castsi128_si256(first), second, 1));
            return;
        }
        auto const *row =
            sizeof(T) == 8 ? byteCompressIndexes.qwords[mask] : compressIndexes.lanes[mask];
        auto indexes = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(row));
        auto expanded = _mm256_cvtepu8_epi32(indexes);
        auto values = _mm256_permutevar8x32_epi32(
            _mm256_loadu_si256(reinterpret_cast<__m256i const *>(in)), expanded);
        auto valid = _mm256_cmpgt_epi32(_mm256_set1_epi32(8), expanded);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), _mm256_and_si256(values, valid));
    }
    static LUTILS_SIMD_INLINE void storePrefix(T *out, T const *packed,
                                               std::size_t count) noexcept {
        auto values = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(packed));
        if constexpr (sizeof(T) == 4) {
            auto mask = _mm256_cmpgt_epi32(_mm256_set1_epi32(static_cast<int>(count)),
                                           _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
            if constexpr (std::is_same_v<T, float>)
                _mm256_maskstore_ps(out, mask, _mm256_castsi256_ps(values));
            else
                _mm256_maskstore_epi32(reinterpret_cast<int *>(out), mask, values);
        } else {
            static_assert(sizeof(T) == 8);
            auto mask = _mm256_cmpgt_epi64(_mm256_set1_epi64x(static_cast<long long>(count)),
                                           _mm256_setr_epi64x(0, 1, 2, 3));
            if constexpr (std::is_same_v<T, double>)
                _mm256_maskstore_pd(out, mask, _mm256_castsi256_pd(values));
            else
                _mm256_maskstore_epi64(reinterpret_cast<long long *>(out), mask, values);
        }
    }
};
} // namespace lutils::simd::detail
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
namespace lutils::simd::detail {
template <class T> struct CompressNative<T, abi::neon> {
    static constexpr bool available = true;
    static LUTILS_SIMD_INLINE void run(T *out, T const *in, std::uint64_t mask) noexcept {
        auto values = vld1q_u8(reinterpret_cast<std::uint8_t const *>(in));
        if constexpr (sizeof(T) == 1) {
            auto lo = vqtbl1q_u8(values, vld1q_u8(byteCompressIndexes.bytes[mask & 255]));
            auto hi = vqtbl1q_u8(vextq_u8(values, values, 8),
                                 vld1q_u8(byteCompressIndexes.bytes[mask >> 8]));
            auto offset = compressIndexes.counts[mask & 255];
            vst1q_u8(reinterpret_cast<std::uint8_t *>(out),
                     vorrq_u8(lo, vqtbl1q_u8(hi, vld1q_u8(byteCompressIndexes.left[offset]))));
        } else {
            if constexpr (sizeof(T) == 8)
                mask = ((mask & 1) ? 3u : 0u) | ((mask & 2) ? 12u : 0u);
            auto const *row =
                sizeof(T) == 2 ? byteCompressIndexes.words[mask] : compressIndexes.bytes[mask];
            vst1q_u8(reinterpret_cast<std::uint8_t *>(out), vqtbl1q_u8(values, vld1q_u8(row)));
        }
    }
};
} // namespace lutils::simd::detail
#endif
