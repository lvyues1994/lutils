#pragma once
#include "Composition.hpp"
#include <algorithm>
#include <lutils/simd.hpp>

namespace composition {
namespace {
namespace s = lutils::simd;
template <Operation Op, class T, class A>
__attribute__((noinline, aligned(64))) std::uint64_t elementwise(Data const &data) {
    using V = s::Batch<T, A>;
    auto a = static_cast<T const *>(data.a), b = static_cast<T const *>(data.b);
    auto c = static_cast<T const *>(data.c), d = static_cast<T const *>(data.d);
    auto out = static_cast<T *>(data.output);
    auto apply = [](V x, V y, V yes, V no) {
        if constexpr (Op == Operation::Min)
            return s::min(x, y);
        else if constexpr (Op == Operation::Max)
            return s::max(x, y);
        else if constexpr (Op == Operation::Fma)
            return s::fma(x, y, yes);
        else
            return s::select(x < y, yes, no);
    };
    std::size_t i = 0;
    for (; data.size - i >= V::size; i += V::size)
        s::store(out + i,
                 apply(s::load<V>(a + i), s::load<V>(b + i), s::load<V>(c + i), s::load<V>(d + i)));
    if (i != data.size) {
        auto n = data.size - i;
        s::store_partial(out + i,
                         apply(s::load_partial<V>(a + i, n), s::load_partial<V>(b + i, n),
                               s::load_partial<V>(c + i, n), s::load_partial<V>(d + i, n)),
                         n);
    }
    return data.size;
}
template <class A>
__attribute__((noinline, aligned(64))) std::uint64_t rearrange(Operation op, Data const &data) {
    using V = s::Batch<float, A>;
    auto a = static_cast<float const *>(data.a), b = static_cast<float const *>(data.b);
    auto out = static_cast<float *>(data.output);
    std::size_t i = 0;
    for (; data.size - i >= V::size; i += V::size) {
        if (op == Operation::Zip) {
            auto pair = s::interleave(s::load<V>(a + i), s::load<V>(b + i));
            s::store(out + 2 * i, pair.low);
            s::store(out + 2 * i + V::size, pair.high);
        } else {
            auto pair = s::deinterleave(s::load<V>(a + 2 * i), s::load<V>(a + 2 * i + V::size));
            s::store(out + i, pair.even);
            s::store(out + data.size + i, pair.odd);
        }
    }
    for (; i < data.size; ++i) {
        if (op == Operation::Zip) {
            out[2 * i] = a[i];
            out[2 * i + 1] = b[i];
        } else {
            out[i] = a[2 * i];
            out[data.size + i] = a[2 * i + 1];
        }
    }
    return 2 * data.size;
}
template <class A>
__attribute__((noinline, aligned(64))) std::uint64_t reduction(Data const &data) {
    using V = s::Batch<std::uint32_t, A>;
    auto in = static_cast<std::uint32_t const *>(data.a);
    auto out = static_cast<std::uint32_t *>(data.output);
    std::size_t i = 0;
    for (; data.size - i >= 8; i += 8) {
        V sum;
        for (std::size_t j = 0; j < 8; j += V::size)
            sum += s::load<V>(in + i + j);
        out[i / 8] = s::reduce_sum(sum);
    }
    if (i != data.size) {
        std::uint32_t sum = 0;
        for (auto j = i; j < data.size; ++j)
            sum += in[j];
        out[i / 8] = sum;
    }
    return (data.size + 7) / 8;
}
template <class A>
__attribute__((noinline, aligned(64))) std::uint64_t run(Operation op, Data const &data) {
    switch (op) {
    case Operation::Min:
        return elementwise<Operation::Min, std::int32_t, A>(data);
    case Operation::Max:
        return elementwise<Operation::Max, std::uint32_t, A>(data);
    case Operation::SelectInt:
        return elementwise<Operation::SelectInt, std::int32_t, A>(data);
    case Operation::SelectFloat:
        return elementwise<Operation::SelectFloat, float, A>(data);
    case Operation::Fma:
        return elementwise<Operation::Fma, float, A>(data);
    case Operation::Zip:
    case Operation::Unzip:
        return rearrange<A>(op, data);
    case Operation::Reduce:
        return reduction<A>(data);
    }
    return 0;
}
} // namespace
} // namespace composition
