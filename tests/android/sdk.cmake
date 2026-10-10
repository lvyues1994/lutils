function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Android SDK check failed: ${ARGV}\n${out}${err}")
    endif()
endfunction()
if(NOT ROOT OR NOT BUILD OR NOT WORK OR NOT TOOLCHAIN)
    message(FATAL_ERROR "Android SDK check requires ROOT, BUILD, WORK and TOOLCHAIN")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
run("${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${WORK}/stage")
file(RENAME "${WORK}/stage" "${WORK}/moved sdk")
foreach(shader IN ITEMS OFF ON)
    if(shader AND NOT HOST_KERNELC)
        continue()
    endif()
    set(consumer "${WORK}/consumer-${shader}")
    run("${CMAKE_COMMAND}" -S "${ROOT}/tests/downstream" -B "${consumer}" -G Ninja
        "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" "-DANDROID_ABI=${ABI}" "-DANDROID_PLATFORM=${PLATFORM}"
        -DANDROID_STL=c++_static "-Dlutils_DIR=${WORK}/moved sdk/lib/cmake/lutils"
        "-DWITH_OPS=${FULL}" "-DWITH_GPU=${GPU}" "-DWITH_SHADER=${shader}"
        "-DLUTILS_HOST_KERNELC=${HOST_KERNELC}" "-DLUTILS_GLSLANG=${GLSLANG}" "-DLUTILS_SPIRV_VAL=${SPIRV_VAL}")
    run("${CMAKE_COMMAND}" --build "${consumer}" -j2)
    run(${EMULATOR} "${consumer}/consumer")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${ROOT}/tests/downstream" -B "${WORK}/invalid-tool" -G Ninja
    "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" "-DANDROID_ABI=${ABI}" "-DANDROID_PLATFORM=${PLATFORM}"
    "-Dlutils_DIR=${WORK}/moved sdk/lib/cmake/lutils" -DWITH_SHADER=ON
    -DLUTILS_HOST_KERNELC=/missing/lutils-kernelc RESULT_VARIABLE invalid OUTPUT_QUIET ERROR_QUIET)
if("${invalid}" STREQUAL "0")
    message(FATAL_ERROR "SDK accepted a missing host compiler")
endif()
