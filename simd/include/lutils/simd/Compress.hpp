#pragma once
#include <lutils/simd/Mask.hpp>
#include <lutils/simd/detail/Compress.hpp>

namespace lutils::simd {
template <class V> struct Compressed {
    V values;
    std::size_t count;
};
template <class T, class A>
LUTILS_SIMD_INLINE Compressed<Batch<T, A>> compress(Batch<T, A> value,
                                                    Mask<T, A> selected) noexcept {
    Compressed<Batch<T, A>> out{{}, 0};
    if constexpr (detail::CompressNative<T, A>::available) {
        detail::CompressNative<T, A>::run(detail::Access::data(out.values),
                                          detail::Access::data(value),
                                          detail::MaskAccess::bits(selected));
        if constexpr (std::is_same_v<A, abi::neon>)
            out.count = detail::compressIndexes.counts[detail::MaskAccess::bits(selected)];
        else
            out.count = count(selected);
        return out;
    }
    for (std::size_t i = 0; i < value.size; ++i)
        if (selected.lane(i))
            out.values.set_lane(out.count++, value.lane(i));
    return out;
}
template <class T, class A>
LUTILS_SIMD_INLINE std::size_t compress_store(T *out, Batch<T, A> value,
                                              Mask<T, A> selected) noexcept {
    assert(out || none(selected));
    if (none(selected))
        return 0;
    if (all(selected)) {
        std::memcpy(out, detail::Access::data(value), sizeof(T) * value.size);
        return value.size;
    }
    if constexpr (detail::CompressNative<T, A>::available) {
        auto packed = compress(value, selected);
        if constexpr (!std::is_same_v<A, abi::neon>) {
            std::memcpy(out, detail::Access::data(packed.values), sizeof(T) * packed.count);
            return packed.count;
        }
        // NEON compression handles four 32-bit lanes. Constant-size copies
        // avoid a dynamic memcpy per batch without over-writing the tail.
        static_assert(sizeof(T) == 4);
        auto const *in = detail::Access::data(packed.values);
        auto remaining = packed.count;
        if (remaining >= 2) {
            std::memcpy(out, in, 2 * sizeof(T));
            out += 2;
            in += 2;
            remaining -= 2;
        }
        if (remaining)
            std::memcpy(out, in, sizeof(T));
        return packed.count;
    }
    std::size_t written = 0;
    for (std::size_t i = 0; i < value.size; ++i)
        if (selected.lane(i))
            out[written++] = value.lane(i);
    return written;
}
} // namespace lutils::simd
