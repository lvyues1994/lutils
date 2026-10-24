#pragma once
// Included inside composition's private namespace, after read().
template <std::size_t N> constexpr std::size_t reverseIndex(std::size_t i) { return N - 1 - i; }
template <std::size_t N, class A>
__attribute__((noinline)) std::uint64_t fixed(Operation op, Data const &data) {
    using V = s::Vec<std::uint32_t, N, A>;
    auto const *a = static_cast<std::uint32_t const *>(data.a);
    auto const *b = static_cast<std::uint32_t const *>(data.b);
    auto *out = static_cast<std::uint32_t *>(data.output);
    std::size_t i = 0;
    for (; data.size - i >= N; i += N) {
        V x, y;
        read(a + i, x, 0);
        read(b + i, y, 0);
        if (op == Operation::FixedAdd)
            s::store(out + i, x + y);
        else if (op == Operation::FixedReverse)
            s::store(out + i, s::permute<N, reverseIndex<N>>(x));
        else {
            auto parts = s::split<N / 2>(x);
            s::store(out + i, s::concat(parts.high, parts.low));
        }
    }
    // A short final block has its own logical width; never shuffle padded lanes.
    auto tail = data.size - i;
    for (std::size_t j = 0; j < tail; ++j)
        out[i + j] = op == Operation::FixedAdd       ? a[i + j] + b[i + j]
                     : op == Operation::FixedReverse ? a[i + tail - 1 - j]
                                                     : a[i + (j + tail / 2) % tail];
    return data.size;
}
template <class A> std::uint64_t fixedRun(Operation op, Data const &data) {
    switch (data.lanes) {
    case 3:
        return fixed<3, A>(op, data);
    case 7:
        return fixed<7, A>(op, data);
    case 8:
        return fixed<8, A>(op, data);
    case 9:
        return fixed<9, A>(op, data);
    case 19:
        return fixed<19, A>(op, data);
    case 64:
        return fixed<64, A>(op, data);
    }
    return 0;
}
