#pragma once
#include <lutils/compute/Shader.hpp>
namespace format_test {
using namespace lutils::compute::kernel;
#define LUTILS_FORMAT_KERNEL(N,G,T,K,C,V) \
    struct LUTILS_KERNEL Copy##N { \
        static constexpr char fileLocation[]="format_" #N; \
        uvec3 local_size{4,1,1}; \
        ImageBinding<gpu::N,Dim::D1,cpu::RGBA32F,3> input; \
        ImageBinding<gpu::N,Dim::D1,cpu::RGBA32F,7> output; \
        void main() { int i=int(gl_GlobalInvocationID.x); imageStore(output,i,imageLoad(input,i)); } \
    };
LUTILS_IMAGE_FORMATS(LUTILS_FORMAT_KERNEL)
#undef LUTILS_FORMAT_KERNEL
}
