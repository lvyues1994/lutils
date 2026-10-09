#include <lutils/compute/Shader.hpp>
namespace group_reject {
using namespace lutils::compute::kernel;
struct LUTILS_KERNEL Kernel {
    static constexpr char fileLocation[] = "reject";
    uvec3 local_size{8, 1, 1};
    BufferBinding<uint, 0> output;
    void helper() { barrier(); }
    void main() {
#if CASE == 1
        if (gl_LocalInvocationIndex == 0u)
            barrier();
#elif CASE == 2
        if (gl_LocalInvocationIndex == 0u)
            return;
        barrier();
#elif CASE == 3
        helper();
#elif CASE == 4
        uint local = 0u;
        atomicAdd(local, 1u);
#elif CASE == 5
        output[0] = atomicAdd(output[1], 1u) + atomicAdd(output[2], 1u);
#elif CASE == 6
        atomicCompSwap(output[0], atomicExchange(output[1], 1u), atomicExchange(output[1], 2u));
#endif
    }
};
} // namespace group_reject
