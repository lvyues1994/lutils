set(source "${WORK}/source with spaces")
set(binary "${WORK}/build")
file(MAKE_DIRECTORY "${source}/private headers")
configure_file("${TEST_DIR}/dependency_project.in" "${source}/CMakeLists.txt" @ONLY)
file(WRITE "${source}/private headers/helper.hpp" "inline unsigned adjust(unsigned n) { return n + FACTOR; }\n")
file(WRITE "${source}/kernel.cpp" [=[
#include <lutils/compute/Kernel.hpp>
#include <helper.hpp>
#ifndef NDEBUG
#error Release macro must reach both CPU and GPU compilation
#endif
#ifdef UNSET_FACTOR
#error Macro undefinition must reach both CPU and GPU compilation
#endif
namespace fixture {
namespace k = lutils::compute::kernel;
struct Params { k::U32 count; };
inline void run(k::Invocation id, k::ReadBuffer source, k::WriteBuffer target, Params p) {
    if (id.x < p.count) target.store(id.x,adjust(source.load(id.x)) + DIRECTORY_FACTOR + CONFIG_FACTOR + GLOBAL_FACTOR + OPTION_FACTOR);
}
}
]=])
file(WRITE "${source}/main.cpp" [=[
#include <fixture.hpp>
#include <iostream>
int main() {
    auto kernel = lutils::generated::fixture();
    std::uint32_t input=2, output=0;
    kernel.cpu({0,0,0},{{&input,1},{&output,1}},{1});
    std::cout << output;
}
]=])
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGN}\n${out}\n${err}")
    endif()
endfunction()
run("${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=-DGLOBAL_FACTOR=5u -D UNSET_FACTOR=1")
run("${CMAKE_COMMAND}" --build "${binary}" -j 2)
execute_process(COMMAND "${binary}/probe" OUTPUT_VARIABLE before RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT before STREQUAL "17")
    message(FATAL_ERROR "CPU adapter inherited consumer macros: ${before}")
endif()
file(SHA256 "${binary}/generated/fixture.spv" original_shader)
# Changed only a transitive header, not the entry source or a declared DEPENDS.
file(WRITE "${source}/private headers/helper.hpp" "inline unsigned adjust(unsigned n) { return n + FACTOR + 4u; }\n")
run("${CMAKE_COMMAND}" --build "${binary}" -j 2)
execute_process(COMMAND "${binary}/probe" OUTPUT_VARIABLE after RESULT_VARIABLE status)
file(SHA256 "${binary}/generated/fixture.spv" new_shader)
if(NOT status EQUAL 0 OR NOT after STREQUAL "21" OR original_shader STREQUAL new_shader)
    message(FATAL_ERROR "transitive header did not rebuild both paths: ${after}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/rejected-build" -G Ninja
    -DREJECT_LANGUAGE_PROFILE=ON RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "language-dependent expressions")
    message(FATAL_ERROR "Language-dependent profile must fail configuration: ${out}\n${err}")
endif()
