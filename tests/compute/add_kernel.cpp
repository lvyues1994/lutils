#include <lutils/compute/Kernel.hpp>
namespace lutils_test {
namespace k = lutils::compute::kernel;
struct Params {
    k::U32 count;
    k::U32 increment;
};
inline k::U32 twice(k::U32 n) {
    return n * 2u;
}
inline void add(k::Invocation id, k::ReadBuffer input, k::WriteBuffer output, Params p) {
    if (id.x < p.count) {
        auto result = twice(input.load(id.x));
        for (k::U32 i = 0u; i < p.increment; ++i) {
            result += 1u;
        }
        output.store(id.x, result);
    }
}
} // namespace lutils_test
