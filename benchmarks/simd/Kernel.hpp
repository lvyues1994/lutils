#pragma once
#include "Micro.hpp"
#include <lutils/simd.hpp>
namespace micro {
namespace s = lutils::simd;
template <class A> std::uint64_t execute(Operation op, Data const &d) {
    using B = s::Batch<std::uint8_t, A>;
    using W = s::Batch<std::uint16_t, A>;
    using I = s::Batch<std::int32_t, A>;
    std::size_t i = 0;
    switch (op) {
    case Operation::Widen:
        for (; d.size - i >= B::size; i += B::size) {
            auto parts = s::widen<std::uint16_t>(s::load<B>(d.bytes + i));
            for (std::size_t p = 0; p < parts.size; ++p)
                s::store(d.wide + i + p * W::size, parts.parts[p]);
        }
        for (; i < d.size; ++i)
            d.wide[i] = d.bytes[i];
        return d.size;
    case Operation::Narrow:
        for (; d.size - i >= B::size; i += B::size) {
            s::WideParts<W, B::size / W::size> parts;
            for (std::size_t p = 0; p < parts.size; ++p)
                parts.parts[p] = s::load<W>(d.words + i + p * W::size);
            s::store(d.narrow + i, s::narrow_saturate<std::uint8_t>(parts));
        }
        for (; i < d.size; ++i)
            d.narrow[i] = static_cast<std::uint8_t>(d.words[i] > 255 ? 255 : d.words[i]);
        return d.size;
    case Operation::Convert:
        for (; d.size - i >= I::size; i += I::size)
            s::store(d.floats + i, s::convert<float>(s::load<I>(d.integers + i)));
        for (; i < d.size; ++i)
            d.floats[i] = static_cast<float>(d.integers[i]);
        return d.size;
    case Operation::Filter:
        return s::copy_if<std::int32_t, A>(d.integers, d.size, d.filtered,
                                           [](I v) { return v > I{}; });
    case Operation::Sum: {
        using U = s::Batch<std::uint32_t, A>;
        U sum;
        for (; d.size - i >= U::size; i += U::size)
            sum = sum + s::bit_cast<std::uint32_t>(s::load<I>(d.integers + i));
        auto result = s::reduce_sum(sum);
        for (; i < d.size; ++i)
            result += static_cast<std::uint32_t>(d.integers[i]);
        return result;
    }
    }
    return 0;
}
} // namespace micro
