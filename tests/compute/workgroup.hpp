#pragma once
#include <lutils/compute/Shader.hpp>
namespace group_test {
using namespace lutils::compute::kernel;
struct LUTILS_KERNEL Reduction {
    static constexpr char fileLocation[] = "reduction";
    uvec3 local_size{4, 2, 1};
    BufferBinding<uint, 0> output;
    SharedArray<uint, 8> partial;
    void main() {
        uint lane = gl_LocalInvocationIndex;
        partial[lane] = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * 16u;
        barrier();
        if (lane == 0u) {
            uint sum = 0u;
            for (uint i = 0u; i < 8u; ++i)
                sum += partial[i];
            output[gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x] = sum;
        }
    }
};
struct LUTILS_KERNEL Histogram {
    static constexpr char fileLocation[] = "histogram";
    uvec3 local_size{8, 1, 1};
    BufferBinding<uint, 0> output;
    SharedArray<uint, 4> counts;
    void main() {
        uint lane = gl_LocalInvocationIndex;
        if (lane < 4u)
            counts[lane] = 0u;
        barrier();
        atomicAdd(counts[lane % 4u], 1u);
        barrier();
        if (lane < 4u)
            atomicAdd(output[lane], counts[lane]);
    }
};
struct LUTILS_KERNEL AtomicOps {
    static constexpr char fileLocation[] = "atomic_ops";
    uvec3 local_size{1, 1, 1};
    BufferBinding<int, 0> values;
    BufferBinding<int, 1> old;
    void main() {
        int a = atomicAdd(values[0], 1);
        old[0] = a;
        int b = atomicMin(values[0], 10);
        old[1] = b;
        int c = atomicMax(values[0], 12);
        old[2] = c;
        int d = atomicAnd(values[0], 10);
        old[3] = d;
        int e = atomicOr(values[0], 3);
        old[4] = e;
        int f = atomicXor(values[0], 2);
        old[5] = f;
        int g = atomicExchange(values[0], 20);
        old[6] = g;
        int h = atomicCompSwap(values[0], 20, 30);
        old[7] = h;
        int i = atomicCompSwap(values[0], 20, 40);
        old[8] = i;
    }
};
struct LUTILS_KERNEL Cancellation {
    static constexpr char fileLocation[] = "cancellation";
    uvec3 local_size{8, 1, 1};
    BufferBinding<uint, 0> output;
    void main() {
        output[gl_LocalInvocationIndex] = 1u;
        barrier();
    }
};
struct LUTILS_KERNEL GlobalCounter {
    static constexpr char fileLocation[] = "global_counter";
    uvec3 local_size{64, 1, 1};
    BufferBinding<uint, 0> output;
    void main() { atomicAdd(output[0], 1u); }
};
} // namespace group_test
