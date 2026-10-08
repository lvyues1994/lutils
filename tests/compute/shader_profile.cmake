set(source "${WORK}/source")
set(binary "${WORK}/build")
file(MAKE_DIRECTORY "${source}/shader/private headers")
configure_file("${TEST_DIR}/shader_profile_project.in" "${source}/CMakeLists.txt" @ONLY)
file(WRITE "${source}/shader/CMakeLists.txt" [=[
add_compile_definitions(DIRECTORY_FACTOR=2u)
add_compile_options(-DOPTION_FACTOR=4u -UUNSET_FACTOR)
lutils_add_shader(fixture NAME fixture SOURCE Kernel.hpp ENTRY fixture::Kernel
    INCLUDE_DIRS "private headers" DEFINITIONS COUNT=2)
target_compile_definitions(fixture PRIVATE FACTOR=3u)
]=])
file(WRITE "${source}/shader/private headers/Types.hpp" [=[
#pragma once
#include <lutils/compute/Shader.hpp>
namespace fixture {
struct Payload { float value; std::array<float,COUNT> extra; };
}
]=])
file(WRITE "${source}/shader/Kernel.hpp" [=[
#pragma once
#include <Types.hpp>
namespace fixture {
using namespace lutils::compute::kernel;
struct LUTILS_KERNEL Kernel {
    static constexpr char fileLocation[]="fixture";
    uvec3 local_size{4,1,1};
    BufferBinding<Payload,0> input;
    BufferBinding<uint,1> output;
    void main() {output[gl_GlobalInvocationID.x]=uint(input[gl_GlobalInvocationID.x].value)+FACTOR+DIRECTORY_FACTOR+GLOBAL_FACTOR+OPTION_FACTOR;}
};
}
]=])
set(main [=[
#include <fixture.hpp>
#include <iostream>
int main() {
    using namespace lutils::compute;
    Backend b{requiredDevice(createCpuDevice())};
    BufferResource<fixture::Payload> input{1}; input[0].value=2.0f;
    BufferResource<unsigned> output{1};
    if(!b.uploadBuffer(&input)||!b.uploadBuffer(&output))return 1;
    fixture::Kernel k;k.input.attach(&input);k.output.attach(&output);
    auto done=b.execute(k,{1,1,1});if(!done || !done.value()->wait())return 1;
    if(!b.downloadBuffer(&output))return 1;
    std::cout<<output[0];
}
]=])
file(WRITE "${source}/main.cpp" "${main}")
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGN}\n${out}\n${err}")
    endif()
endfunction()
run("${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_CXX_FLAGS=-DGLOBAL_FACTOR=5u -DUNSET_FACTOR=1")
run("${CMAKE_COMMAND}" --build "${binary}" -j2)
execute_process(COMMAND "${binary}/probe" OUTPUT_VARIABLE value RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT value STREQUAL "16")
    message(FATAL_ERROR "shader public preprocessing profile failed: ${value}")
endif()
file(WRITE "${source}/main.cpp" "#undef FACTOR\n#define FACTOR 99u\n${main}")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" -j2 RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT "${out}${err}" MATCHES "shader macro mismatch: FACTOR")
    message(FATAL_ERROR "conflicting consumer macro must fail: ${out}\n${err}")
endif()
foreach(kind IN ITEMS function)
    execute_process(COMMAND "${TOOL}" "${source}/shader/Kernel.hpp" --entry=fixture::Kernel
        --name=fixture --output=${WORK}/wrong --expect-kind=${kind} -- -std=c++17
        "-I${ROOT}/compute/kernel/include" "-I${source}/shader/private headers"
        -DCOUNT=2 -DFACTOR=3u -DDIRECTORY_FACTOR=2u -DGLOBAL_FACTOR=5u -DOPTION_FACTOR=4u
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "require lutils_add_shader")
        message(FATAL_ERROR "wrong CMake entry point must fail: ${out}\n${err}")
    endif()
endforeach()
