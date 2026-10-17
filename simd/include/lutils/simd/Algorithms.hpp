#pragma once
#include <lutils/simd/Arithmetic.hpp>
#include <lutils/simd/Compress.hpp>
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
// Output must hold n elements. Exact in-place filtering is supported; other overlap is not.
template <class T, class A, class Predicate>
LUTILS_SIMD_INLINE std::size_t copy_if(T const *input, std::size_t n, T *output,
                                       Predicate predicate) {
    using V = Batch<T, A>;
    assert(n == 0 || (input && output));
    std::size_t i = 0, written = 0;
    while (n - i >= V::size) {
        auto values = load<V>(input + i);
        written += compress_store(output + written, values, predicate(values));
        i += V::size;
    }
    if (i < n) {
        auto values = load_partial<V>(input + i, n - i);
        written += compress_store(output + written, values, predicate(values) & first_n<V>(n - i));
    }
    return written;
}
} // namespace lutils::simd
