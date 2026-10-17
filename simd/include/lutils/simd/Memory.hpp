#pragma once
#include <lutils/simd/Batch.hpp>

namespace lutils::simd {
template <class V> LUTILS_SIMD_INLINE V load(typename V::value_type const *p) noexcept {
    assert(p);
    V out;
    std::memcpy(detail::Access::data(out), p, sizeof(typename V::value_type) * V::size);
    return out;
}
template <class T, class A> LUTILS_SIMD_INLINE void store(T *p, Batch<T, A> v) noexcept {
    assert(p);
    std::memcpy(p, detail::Access::data(v), sizeof(T) * v.size);
}
template <class V>
LUTILS_SIMD_INLINE V load_partial(typename V::value_type const *p, std::size_t n,
                                         typename V::value_type fill = {}) noexcept {
    assert(n <= V::size);
    V out{fill};
    if (n) {
        assert(p);
        std::memcpy(detail::Access::data(out), p, n * sizeof(typename V::value_type));
    }
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE void store_partial(T *p, Batch<T, A> v, std::size_t n) noexcept {
    assert(n <= v.size);
    if (n) {
        assert(p);
        std::memcpy(p, detail::Access::data(v), n * sizeof(T));
    }
}
template <class V>
LUTILS_SIMD_INLINE V load_aligned(typename V::value_type const *p) noexcept {
    assert(reinterpret_cast<std::uintptr_t>(p) % V::alignment == 0);
    return load<V>(p);
}
template <class T, class A>
LUTILS_SIMD_INLINE void store_aligned(T *p, Batch<T, A> v) noexcept {
    assert((reinterpret_cast<std::uintptr_t>(p) % Batch<T, A>::alignment == 0));
    store(p, v);
}
} // namespace lutils::simd
