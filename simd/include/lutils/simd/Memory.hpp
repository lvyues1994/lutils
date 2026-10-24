#pragma once
#include <lutils/simd/Batch.hpp>
#include <lutils/simd/Mask.hpp>
#include <optional>

namespace lutils::simd {
// Non-owning element range. The caller supplies a valid allocation and lifetime.
template <class T> class MemoryView {
  public:
    LUTILS_SIMD_INLINE constexpr MemoryView(T *data, std::size_t size) noexcept
        : data_(data), size_(size) {
        assert(data || size == 0);
    }
    template <std::size_t N>
    LUTILS_SIMD_INLINE constexpr MemoryView(T (&data)[N]) noexcept : data_(data), size_(N) {}
    LUTILS_SIMD_INLINE constexpr T *data() const noexcept { return data_; }
    LUTILS_SIMD_INLINE constexpr std::size_t size() const noexcept { return size_; }

  private:
    T *data_;
    std::size_t size_;
};
// Reference output avoids aggregate-return scalarization in some ARM compilers.
// The source can alias the destination storage; transfer still has value semantics.
template <class T, class A> LUTILS_SIMD_INLINE void load(T const *p, Batch<T, A> &out) noexcept {
    assert(p);
    std::memmove(detail::Access::data(out), p, sizeof(T) * out.size);
}
template <class V> LUTILS_SIMD_INLINE V load(typename V::value_type const *p) noexcept {
    assert(p);
    V out;
    std::memcpy(detail::Access::data(out), p, sizeof(typename V::value_type) * V::size);
    return out;
}
template <class T, class A> LUTILS_SIMD_INLINE void store(T *p, Batch<T, A> const &v) noexcept {
    assert(p);
    std::memmove(p, detail::Access::data(v), sizeof(T) * v.size);
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
LUTILS_SIMD_INLINE void store_partial(T *p, Batch<T, A> const &v, std::size_t n) noexcept {
    assert(n <= v.size);
    if (n) {
        assert(p);
        std::memmove(p, detail::Access::data(v), n * sizeof(T));
    }
}
template <class V> LUTILS_SIMD_INLINE V load_aligned(typename V::value_type const *p) noexcept {
    assert(reinterpret_cast<std::uintptr_t>(p) % V::alignment == 0);
    return load<V>(p);
}
template <class T, class A>
LUTILS_SIMD_INLINE void store_aligned(T *p, Batch<T, A> const &v) noexcept {
    assert((reinterpret_cast<std::uintptr_t>(p) % Batch<T, A>::alignment == 0));
    store(p, v);
}
template <class V> struct PartialLoad {
    V value;
    typename V::mask_type valid;
    std::size_t count;
};
template <class V, class T>
LUTILS_SIMD_INLINE std::optional<V> try_load(MemoryView<T> source,
                                             std::size_t offset = 0) noexcept {
    static_assert(std::is_same_v<std::remove_const_t<T>, typename V::value_type>);
    if (offset > source.size() || source.size() - offset < V::size)
        return std::nullopt;
    return load<V>(source.data() + offset);
}
template <class V, class T>
LUTILS_SIMD_INLINE PartialLoad<V> load_partial(MemoryView<T> source, std::size_t offset = 0,
                                               typename V::value_type fill = {}) noexcept {
    static_assert(std::is_same_v<std::remove_const_t<T>, typename V::value_type>);
    auto n = offset >= source.size() ? 0 : source.size() - offset;
    if (n > V::size)
        n = V::size;
    return {load_partial<V>(n ? source.data() + offset : nullptr, n, fill), first_n<V>(n), n};
}
template <class T, class A>
LUTILS_SIMD_INLINE bool try_store(MemoryView<T> destination, Batch<T, A> const &value,
                                  std::size_t offset = 0) noexcept {
    if (offset > destination.size() || destination.size() - offset < value.size)
        return false;
    store(destination.data() + offset, value);
    return true;
}
template <class T, class A>
LUTILS_SIMD_INLINE std::size_t store_partial(MemoryView<T> destination, Batch<T, A> const &value,
                                             std::size_t offset = 0) noexcept {
    auto n = offset >= destination.size() ? 0 : destination.size() - offset;
    if (n > value.size)
        n = value.size;
    store_partial(n ? destination.data() + offset : nullptr, value, n);
    return n;
}
} // namespace lutils::simd
