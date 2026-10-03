#include <lutils/compute/Kernel.hpp>
namespace reject_test {
namespace k = lutils::compute::kernel;
#if CASE == 3
inline unsigned calls = 0;
struct Params {
    k::U32 count = ++calls;
};
#elif CASE == 4
union Params {
    k::U32 count;
    float other;
};
#elif CASE == 5
struct Params {
    volatile k::U32 count;
};
#else
struct Params {
    k::U32 count;
};
#endif
#if CASE == 1
inline k::U32 helper(k::U32 n) {
    auto p = new k::U32{n};
    auto value = *p;
    delete p;
    return value;
}
#elif CASE == 2
inline k::U32 helper(k::U32 n) {
    return n ? helper(n - 1u) : 0u;
}
#else
inline k::U32 helper(k::U32 n) {
    return n;
}
#endif
inline void run(k::Invocation id, k::ReadBuffer input, k::WriteBuffer output, Params p) {
#if CASE == 6
    auto i = id.x;
    i = i++;
    output.store(id.x, i);
#elif CASE == 7
    auto i = id.x;
    output.store(id.x, helper(i++));
#elif CASE == 8
    auto index = static_cast<long>(id.x);
    output.store(id.x, static_cast<k::U32>(index));
#elif CASE == 9
    static k::U32 count = 0;
    output.store(id.x, ++count);
#else
    if (id.x < p.count)
        output.store(id.x, helper(input.load(id.x)));
#endif
}
} // namespace reject_test
