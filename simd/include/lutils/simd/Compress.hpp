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
        if constexpr (std::is_same_v<A, abi::neon> && Batch<T, A>::size <= 8)
            out.count = detail::compressIndexes.counts[detail::MaskAccess::bits(selected)];
        else if constexpr (std::is_same_v<A, abi::neon>) {
            auto bits = detail::MaskAccess::bits(selected);
            out.count = detail::compressIndexes.counts[bits & 255] +
                        detail::compressIndexes.counts[bits >> 8];
        } else
            out.count = count(selected);
        return out;
    }
    for (std::size_t i = 0; i < value.size; ++i)
        if (selected.lane(i))
            out.values.set_lane(out.count++, value.lane(i));
    return out;
}
namespace detail {
// The caller provides a valid output range. Keep pointer validation at the
// public boundary so algorithms with validated output do not repeat it.
template <class T, class A>
LUTILS_SIMD_INLINE std::size_t compressStore(T *out, Batch<T, A> value,
                                          Mask<T, A> selected) noexcept {
    // This is the scalar decision boundary. Reuse one compact mask for the
    // empty/full checks and the two-lane store instead of extracting lanes again.
    auto bits = detail::MaskAccess::bits(selected);
    if (!bits)
        return 0;
    assert(out);
    if (bits == detail::validMaskBits<Batch<T, A>::size>) {
        std::memcpy(out, detail::Access::data(value), sizeof(T) * value.size);
        return value.size;
    }
    if constexpr (std::is_same_v<A, abi::neon> && sizeof(T) == 8) {
        // Two lanes and a partial nonempty mask: exactly one element survives.
        auto element = (bits & 1) ? value.lane(0) : value.lane(1);
        std::memcpy(out, &element, sizeof(T));
        return 1;
    }
    if constexpr (detail::CompressNative<T, A>::available) {
        auto packed = compress(value, selected);
        if constexpr (std::is_same_v<A, abi::avx2> && sizeof(T) >= 4) {
            detail::CompressNative<T, A>::storePrefix(out, detail::Access::data(packed.values),
                                                      packed.count);
            return packed.count;
        }
        if constexpr (!std::is_same_v<A, abi::neon>) {
            std::memcpy(out, detail::Access::data(packed.values), sizeof(T) * packed.count);
            return packed.count;
        }
        if constexpr (sizeof(T) == 4) {
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
        // Partial NEON stores contain fewer than 16 bytes; never over-store.
        auto const *in = reinterpret_cast<std::byte const *>(detail::Access::data(packed.values));
        auto *dest = reinterpret_cast<std::byte *>(out);
        auto bytes = sizeof(T) * packed.count;
        if (bytes & 8) {
            std::memcpy(dest, in, 8);
            dest += 8;
            in += 8;
        }
        if (bytes & 4) {
            std::memcpy(dest, in, 4);
            dest += 4;
            in += 4;
        }
        if (bytes & 2) {
            std::memcpy(dest, in, 2);
            dest += 2;
            in += 2;
        }
        if (bytes & 1)
            std::memcpy(dest, in, 1);
        return packed.count;
    }
    std::size_t written = 0;
    for (std::size_t i = 0; i < value.size; ++i)
        if ((bits >> i) & 1)
            out[written++] = value.lane(i);
    return written;
}
} // namespace detail
template <class T, class A>
LUTILS_SIMD_INLINE std::size_t compress_store(T *out, Batch<T, A> value,
                                           Mask<T, A> selected) noexcept {
    // GCC's fortified memcpy diagnostics require the explicit null branch even
    // when an empty canonical mask makes every store unreachable.
    if (!out) {
        assert(none(selected));
        return 0;
    }
    return detail::compressStore(out, value, selected);
}
} // namespace lutils::simd
