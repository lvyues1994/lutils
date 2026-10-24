#pragma once
#include <lutils/simd/Arithmetic.hpp>
#include <lutils/simd/Compress.hpp>
#include <lutils/simd/FilterWrite.hpp>
#include <lutils/simd/Memory.hpp>

namespace lutils::simd {
template <class T, class A>
LUTILS_SIMD_INLINE std::optional<std::size_t> mismatch(T const *a, T const *b,
                                                       std::size_t n) noexcept {
    using V = Batch<T, A>;
    std::size_t i = 0;
    while (n - i >= V::size) {
        auto found = first_true(load<V>(a + i) != load<V>(b + i));
        if (found)
            return i + *found;
        i += V::size;
    }
    if (i < n) {
        auto found = first_true((load_partial<V>(a + i, n - i) != load_partial<V>(b + i, n - i)) &
                                first_n<V>(n - i));
        if (found)
            return i + *found;
    }
    return std::nullopt;
}
template <class T, class A, class Predicate>
LUTILS_SIMD_INLINE std::optional<std::size_t> find_if(T const *input, std::size_t n,
                                                      Predicate predicate) {
    using V = Batch<T, A>;
    std::size_t i = 0;
    while (n - i >= V::size) {
        auto found = first_true(predicate(load<V>(input + i)));
        if (found)
            return i + *found;
        i += V::size;
    }
    if (i < n) {
        auto found = first_true(predicate(load_partial<V>(input + i, n - i)) & first_n<V>(n - i));
        if (found)
            return i + *found;
    }
    return std::nullopt;
}
namespace detail {
template <FilterWrite Write, class T, class A, class Predicate>
LUTILS_SIMD_INLINE std::size_t copyIf(T const *input, std::size_t n, T *output,
                                      Predicate &predicate) {
    using V = Batch<T, A>;
    assert(n == 0 || (input && output));
    std::size_t i = 0, written = 0;
    while (n - i >= V::size) {
        V values;
        load(input + i, values);
        auto selected = predicate(values);
        // Keep these loops' count update in the writer. An outer empty-mask
        // branch regressed short Scalar filters and x86 full/partial masks.
        if constexpr (V::size == 1 || std::is_same_v<A, abi::sse2> ||
                      (std::is_same_v<A, abi::avx2> && Write == FilterWrite::Exact)) {
            written += compressStoreBits(output + written, values, MaskAccess::bits(selected));
        } else if constexpr (std::is_same_v<A, abi::avx2> && Write == FilterWrite::AllowOverwrite) {
            auto bits = MaskAccess::bits(selected);
            if (sizeof(T) <= 2 && bits == validMaskBits<V::size>) {
                store(output + written, values);
                written += V::size;
            } else if (bits) {
                auto packed = compressBits(values, bits);
                store(output + written, packed.values);
                written += packed.count;
            }
        } else {
            auto bits = MaskAccess::bits(selected);
            if (bits) {
                if constexpr (Write == FilterWrite::AllowOverwrite &&
                              CompressNative<T, A>::available) {
                    if (sizeof(T) <= 2 && bits == validMaskBits<V::size>) {
                        store(output + written, values);
                        written += V::size;
                    } else {
                        auto packed = compressBits(values, bits);
                        // written <= i, including exact in-place filtering.
                        store(output + written, packed.values);
                        written += packed.count;
                    }
                } else
                    written += compressStoreBits(output + written, values, bits);
            }
        }
        i += V::size;
    }
    if (i < n) {
        auto values = load_partial<V>(input + i, n - i);
        auto selected = predicate(values);
        // The tail has fewer than V::size writable elements. Keep that bound
        // explicit instead of relying on the optimizer to infer it from a mask.
        for (std::size_t lane = 0; lane < n - i; ++lane)
            if (selected.lane(lane))
                output[written++] = values.lane(lane);
    }
    return written;
}
} // namespace detail
// Output must hold n elements. Exact in-place filtering is supported; other
// overlap is not. The four-argument overload preserves output[count, n).
template <class T, class A, class Predicate>
LUTILS_SIMD_INLINE std::size_t copy_if(T const *input, std::size_t n, T *output,
                                       Predicate predicate) {
    return detail::copyIf<FilterWrite::Exact, T, A>(input, n, output, predicate);
}
template <class T, class A, class Predicate>
LUTILS_SIMD_INLINE std::size_t copy_if(T const *input, std::size_t n, T *output,
                                       Predicate predicate, FilterWrite write) {
    if (write == FilterWrite::AllowOverwrite)
        return detail::copyIf<FilterWrite::AllowOverwrite, T, A>(input, n, output, predicate);
    return detail::copyIf<FilterWrite::Exact, T, A>(input, n, output, predicate);
}
} // namespace lutils::simd
