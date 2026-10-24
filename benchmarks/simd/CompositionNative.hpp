#pragma once
#include "Composition.hpp"
#include <algorithm>
#include <cmath>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace composition {
namespace {
// Independent intrinsic reference; never calls the library under test.
template <Operation op>
__attribute__((noinline, aligned(64))) std::uint64_t native(Data const &data) {
    auto ai = static_cast<std::int32_t const *>(data.a);
    auto bi = static_cast<std::int32_t const *>(data.b);
    auto ci = static_cast<std::int32_t const *>(data.c);
    auto di = static_cast<std::int32_t const *>(data.d);
    auto au = static_cast<std::uint32_t const *>(data.a);
    auto bu = static_cast<std::uint32_t const *>(data.b);
    auto a = static_cast<float const *>(data.a), b = static_cast<float const *>(data.b);
    auto c = static_cast<float const *>(data.c), d = static_cast<float const *>(data.d);
    auto oi = static_cast<std::int32_t *>(data.output);
    auto ou = static_cast<std::uint32_t *>(data.output);
    auto out = static_cast<float *>(data.output);
    std::size_t i = 0;
    if (op == Operation::Reduce) {
        for (; data.size - i >= 8; i += 8) {
#if defined(__AVX2__)
            auto x = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(au + i));
            auto y = _mm_add_epi32(_mm256_castsi256_si128(x), _mm256_extracti128_si256(x, 1));
            y = _mm_add_epi32(y, _mm_shuffle_epi32(y, 0x4e));
            y = _mm_add_epi32(y, _mm_shuffle_epi32(y, 0xb1));
            ou[i / 8] = static_cast<std::uint32_t>(_mm_cvtsi128_si32(y));
#elif defined(__SSE2__)
            auto y = _mm_add_epi32(_mm_loadu_si128(reinterpret_cast<__m128i const *>(au + i)),
                                   _mm_loadu_si128(reinterpret_cast<__m128i const *>(au + i + 4)));
            y = _mm_add_epi32(y, _mm_shuffle_epi32(y, 0x4e));
            y = _mm_add_epi32(y, _mm_shuffle_epi32(y, 0xb1));
            ou[i / 8] = static_cast<std::uint32_t>(_mm_cvtsi128_si32(y));
#elif defined(__aarch64__)
            ou[i / 8] = vaddvq_u32(vaddq_u32(vld1q_u32(au + i), vld1q_u32(au + i + 4)));
#endif
        }
        if (i != data.size) {
            std::uint32_t sum = 0;
            for (auto j = i; j < data.size; ++j)
                sum += au[j];
            ou[i / 8] = sum;
        }
        return (data.size + 7) / 8;
    }
#if defined(__AVX2__)
    constexpr std::size_t width = 8;
#else
    constexpr std::size_t width = 4;
#endif
    for (; data.size - i >= width; i += width) {
        if (op == Operation::Min || op == Operation::Max || op == Operation::SelectInt) {
#if defined(__AVX2__)
            auto x = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(ai + i));
            auto y = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(bi + i));
            auto v = op == Operation::Min ? _mm256_min_epi32(x, y) : _mm256_max_epu32(x, y);
            if (op == Operation::SelectInt)
                v = _mm256_blendv_epi8(
                    _mm256_loadu_si256(reinterpret_cast<__m256i const *>(di + i)),
                    _mm256_loadu_si256(reinterpret_cast<__m256i const *>(ci + i)),
                    _mm256_cmpgt_epi32(y, x));
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(oi + i), v);
#elif defined(__SSE2__)
            auto x = _mm_loadu_si128(reinterpret_cast<__m128i const *>(ai + i));
            auto y = _mm_loadu_si128(reinterpret_cast<__m128i const *>(bi + i));
            auto sign = _mm_set1_epi32(INT32_MIN);
            auto m = op == Operation::Max
                         ? _mm_cmpgt_epi32(_mm_xor_si128(x, sign), _mm_xor_si128(y, sign))
                         : _mm_cmpgt_epi32(y, x);
            if (op == Operation::SelectInt) {
                x = _mm_loadu_si128(reinterpret_cast<__m128i const *>(ci + i));
                y = _mm_loadu_si128(reinterpret_cast<__m128i const *>(di + i));
            }
            _mm_storeu_si128(reinterpret_cast<__m128i *>(oi + i),
                             _mm_or_si128(_mm_and_si128(m, x), _mm_andnot_si128(m, y)));
#elif defined(__aarch64__)
            auto x = vld1q_s32(ai + i), y = vld1q_s32(bi + i);
            auto v = op == Operation::Min
                         ? vminq_s32(x, y)
                         : vreinterpretq_s32_u32(
                               vmaxq_u32(vreinterpretq_u32_s32(x), vreinterpretq_u32_s32(y)));
            if (op == Operation::SelectInt)
                v = vbslq_s32(vcltq_s32(x, y), vld1q_s32(ci + i), vld1q_s32(di + i));
            vst1q_s32(oi + i, v);
#endif
        } else if (op == Operation::Zip || op == Operation::Unzip) {
#if defined(__AVX2__)
            if (op == Operation::Zip) {
                auto x = _mm256_loadu_ps(a + i), y = _mm256_loadu_ps(b + i);
                auto lo = _mm256_unpacklo_ps(x, y), hi = _mm256_unpackhi_ps(x, y);
                _mm256_storeu_ps(out + 2 * i, _mm256_permute2f128_ps(lo, hi, 0x20));
                _mm256_storeu_ps(out + 2 * i + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
            } else {
                auto x = _mm256_loadu_ps(a + 2 * i), y = _mm256_loadu_ps(a + 2 * i + 8);
                auto lo = _mm256_permute2f128_ps(x, y, 0x20),
                     hi = _mm256_permute2f128_ps(x, y, 0x31);
                _mm256_storeu_ps(out + i, _mm256_shuffle_ps(lo, hi, 0x88));
                _mm256_storeu_ps(out + data.size + i, _mm256_shuffle_ps(lo, hi, 0xdd));
            }
#elif defined(__SSE2__)
            if (op == Operation::Zip) {
                auto x = _mm_loadu_ps(a + i), y = _mm_loadu_ps(b + i);
                _mm_storeu_ps(out + 2 * i, _mm_unpacklo_ps(x, y));
                _mm_storeu_ps(out + 2 * i + 4, _mm_unpackhi_ps(x, y));
            } else {
                auto x = _mm_loadu_ps(a + 2 * i), y = _mm_loadu_ps(a + 2 * i + 4);
                _mm_storeu_ps(out + i, _mm_shuffle_ps(x, y, 0x88));
                _mm_storeu_ps(out + data.size + i, _mm_shuffle_ps(x, y, 0xdd));
            }
#elif defined(__aarch64__)
            if (op == Operation::Zip) {
                auto x = vld1q_f32(a + i), y = vld1q_f32(b + i);
                vst1q_f32(out + 2 * i, vzip1q_f32(x, y));
                vst1q_f32(out + 2 * i + 4, vzip2q_f32(x, y));
            } else {
                auto x = vld1q_f32(a + 2 * i), y = vld1q_f32(a + 2 * i + 4);
                vst1q_f32(out + i, vuzp1q_f32(x, y));
                vst1q_f32(out + data.size + i, vuzp2q_f32(x, y));
            }
#endif
        } else {
#if defined(__AVX2__)
            auto x = _mm256_loadu_ps(a + i), y = _mm256_loadu_ps(b + i);
            if (op == Operation::SelectFloat)
                _mm256_storeu_ps(out + i,
                                 _mm256_blendv_ps(_mm256_loadu_ps(d + i), _mm256_loadu_ps(c + i),
                                                  _mm256_cmp_ps(x, y, _CMP_LT_OQ)));
#if defined(__FMA__)
            else
                _mm256_storeu_ps(out + i, _mm256_fmadd_ps(x, y, _mm256_loadu_ps(c + i)));
#else
            else
                for (std::size_t j = 0; j < width; ++j)
                    out[i + j] = std::fma(a[i + j], b[i + j], c[i + j]);
#endif
#elif defined(__SSE2__)
            auto m = _mm_cmplt_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i));
            if (op == Operation::SelectFloat)
                _mm_storeu_ps(out + i, _mm_or_ps(_mm_and_ps(m, _mm_loadu_ps(c + i)),
                                                 _mm_andnot_ps(m, _mm_loadu_ps(d + i))));
            else
                for (std::size_t j = 0; j < width; ++j)
                    out[i + j] = std::fma(a[i + j], b[i + j], c[i + j]);
#elif defined(__aarch64__)
            auto x = vld1q_f32(a + i), y = vld1q_f32(b + i);
            auto v = op == Operation::SelectFloat
                         ? vbslq_f32(vcltq_f32(x, y), vld1q_f32(c + i), vld1q_f32(d + i))
                         : vfmaq_f32(vld1q_f32(c + i), x, y);
            vst1q_f32(out + i, v);
#endif
        }
    }
    for (; i < data.size; ++i) {
        switch (op) {
        case Operation::Min:
            oi[i] = std::min(ai[i], bi[i]);
            break;
        case Operation::Max:
            ou[i] = std::max(au[i], bu[i]);
            break;
        case Operation::SelectInt:
            oi[i] = ai[i] < bi[i] ? ci[i] : di[i];
            break;
        case Operation::SelectFloat:
            out[i] = a[i] < b[i] ? c[i] : d[i];
            break;
        case Operation::Fma:
            out[i] = std::fma(a[i], b[i], c[i]);
            break;
        case Operation::Zip:
            out[2 * i] = a[i];
            out[2 * i + 1] = b[i];
            break;
        case Operation::Unzip:
            out[i] = a[2 * i];
            out[data.size + i] = a[2 * i + 1];
            break;
        case Operation::Reduce:
            break;
        }
    }
    return op == Operation::Zip || op == Operation::Unzip ? 2 * data.size : data.size;
}
__attribute__((noinline, aligned(64))) inline std::uint64_t nativeRun(Operation op,
                                                                      Data const &data) {
    switch (op) {
    case Operation::Min:
        return native<Operation::Min>(data);
    case Operation::Max:
        return native<Operation::Max>(data);
    case Operation::SelectInt:
        return native<Operation::SelectInt>(data);
    case Operation::SelectFloat:
        return native<Operation::SelectFloat>(data);
    case Operation::Zip:
        return native<Operation::Zip>(data);
    case Operation::Unzip:
        return native<Operation::Unzip>(data);
    case Operation::Reduce:
        return native<Operation::Reduce>(data);
    case Operation::Fma:
        return native<Operation::Fma>(data);
    }
    return 0;
}
} // namespace
} // namespace composition
