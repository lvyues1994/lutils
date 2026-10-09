function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGN}\n${out}\n${err}")
    endif()
endfunction()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${ROOT}/tests/downstream/" DESTINATION "${WORK}/source")
set(source "${WORK}/source")
set(common -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=${COMPILER}")
if(MODE STREQUAL "source")
    run("${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/consumer" ${common}
        "-DSOURCE_ROOT=${ROOT}" -DWITH_SHADER=${FULL} -DWITH_OPS=${FULL}
        "-DLLVM_DIR=${LLVM_DIR}" "-DLUTILS_CLANG_INCLUDE_DIR=${CLANG_INCLUDE}"
        "-DLUTILS_CLANG_CPP_LIBRARY=${CLANG_LIBRARY}"
        "-DLUTILS_GLSLANG=${GLSLANG}" "-DLUTILS_SPIRV_VAL=${SPIRV_VAL}")
else()
    set(prefix "${WORK}/prefix original")
    run("${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${prefix}")
    file(RENAME "${prefix}" "${WORK}/prefix moved")
    set(prefix "${WORK}/prefix moved")
    file(GLOB_RECURSE public_files "${prefix}/*.cmake" "${prefix}/*.hpp")
    foreach(path IN LISTS public_files)
        file(READ "${path}" text)
        if(text MATCHES "${ROOT}|prefix original")
            message(FATAL_ERROR "Installed interface refers to original paths: ${path}")
        endif()
    endforeach()
    # Basic consumers and optional components must not require Vulkan or kernel tools.
    run("${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/basic" ${common}
        "-DCMAKE_PREFIX_PATH=${prefix}" -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE
        -DOPTIONAL_COMPONENT=vulkan)
    run("${CMAKE_COMMAND}" --build "${WORK}/basic" -j2)
    run("${WORK}/basic/consumer")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/missing"
        ${common} "-DCMAKE_PREFIX_PATH=${prefix}" -DREQUEST_COMPONENT=missing_component
        RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
    if(status EQUAL 0)
        message(FATAL_ERROR "Unknown required component was accepted")
    endif()
    if(FULL)
        # This project must use the precompiled conversion without finding shader tools.
        run("${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/ops" ${common}
            "-DCMAKE_PREFIX_PATH=${prefix}" -DWITH_OPS=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE -DLUTILS_GLSLANG=/missing/glslang
            -DLUTILS_SPIRV_VAL=/missing/spirv-val)
        run("${CMAKE_COMMAND}" --build "${WORK}/ops" -j2)
        run("${WORK}/ops/consumer")
    endif()
    run("${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/consumer" ${common}
        "-DCMAKE_PREFIX_PATH=${prefix}" -DWITH_SHADER=${FULL} -DWITH_OPS=${FULL}
        -DWITH_GPU=${GPU} "-DLUTILS_GLSLANG=${GLSLANG}" "-DLUTILS_SPIRV_VAL=${SPIRV_VAL}")
endif()
run("${CMAKE_COMMAND}" --build "${WORK}/consumer" -j2)
run("${WORK}/consumer/consumer")
if(FULL)
    file(WRITE "${source}/private headers/Factor.hpp" "#pragma once\n#define FACTOR 9.0f\n")
    run("${CMAKE_COMMAND}" --build "${WORK}/consumer" -j2)
    run("${WORK}/consumer/consumer")
endif()
