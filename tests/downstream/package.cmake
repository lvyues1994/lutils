function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGN}\n${out}\n${err}")
    endif()
endfunction()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/binary" "${WORK}/source")
run("${CPACK}" --config "${BUILD}/CPackConfig.cmake" -B "${WORK}/binary")
run("${CPACK}" --config "${BUILD}/CPackSourceConfig.cmake" -B "${WORK}/source")
foreach(kind IN ITEMS binary source)
    file(GLOB packages "${WORK}/${kind}/*.tar.gz")
    list(LENGTH packages count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one ${kind} TGZ package")
    endif()
    list(GET packages 0 package)
    file(ARCHIVE_EXTRACT INPUT "${package}" DESTINATION "${WORK}/${kind}/extracted")
    file(GLOB roots LIST_DIRECTORIES TRUE "${WORK}/${kind}/extracted/*")
    list(GET roots 0 root)
    file(RENAME "${root}" "${WORK}/${kind}/relocated package")
endforeach()
run("${CMAKE_COMMAND}" -S "${ROOT}/tests/downstream" -B "${WORK}/consumer" -G Ninja
    "-DCMAKE_CXX_COMPILER=${COMPILER}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=${WORK}/binary/relocated package" -DWITH_SHADER=${FULL}
    -DWITH_OPS=${FULL} -DWITH_GPU=${GPU}
    "-DLUTILS_GLSLANG=${GLSLANG}" "-DLUTILS_SPIRV_VAL=${SPIRV_VAL}")
run("${CMAKE_COMMAND}" --build "${WORK}/consumer" -j2)
run("${WORK}/consumer/consumer")
run("${CMAKE_COMMAND}" -S "${WORK}/source/relocated package" -B "${WORK}/source-build" -G Ninja
    "-DCMAKE_CXX_COMPILER=${COMPILER}" -DCMAKE_BUILD_TYPE=Release
    -DLUTILS_BUILD_EXAMPLES=OFF -DLUTILS_INSTALL=OFF)
run("${CMAKE_COMMAND}" --build "${WORK}/source-build" -j2)
run("${CTEST}" --test-dir "${WORK}/source-build" --output-on-failure)
if(FULL)
    run("${CMAKE_COMMAND}" -S "${WORK}/source/relocated package/tests/downstream"
        -B "${WORK}/source-consumer" -G Ninja -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_CXX_COMPILER=${COMPILER}" "-DSOURCE_ROOT=${WORK}/source/relocated package"
        -DWITH_SHADER=ON -DWITH_OPS=ON -DWITH_GPU=${GPU} "-DLLVM_DIR=${LLVM_DIR}"
        "-DLUTILS_CLANG_INCLUDE_DIR=${CLANG_INCLUDE}" "-DLUTILS_CLANG_CPP_LIBRARY=${CLANG_LIBRARY}"
        "-DLUTILS_GLSLANG=${GLSLANG}" "-DLUTILS_SPIRV_VAL=${SPIRV_VAL}")
    run("${CMAKE_COMMAND}" --build "${WORK}/source-consumer" -j2)
    run("${WORK}/source-consumer/consumer")
endif()
# Packaging a source tree below a directory called build must not exclude the entire tree.
run("${CMAKE_COMMAND}" -S "${WORK}/source/relocated package" -B "${WORK}/repackage-build"
    -G Ninja -DLUTILS_BUILD_TESTS=OFF -DLUTILS_BUILD_EXAMPLES=OFF)
run("${CPACK}" --config "${WORK}/repackage-build/CPackSourceConfig.cmake" -B "${WORK}/repackage")
file(GLOB repackages "${WORK}/repackage/*.tar.gz")
list(GET repackages 0 repackage)
file(ARCHIVE_EXTRACT INPUT "${repackage}" DESTINATION "${WORK}/repackage/extracted")
file(GLOB roots "${WORK}/repackage/extracted/*/CMakeLists.txt")
if(NOT roots)
    message(FATAL_ERROR "Repackaged source archive is empty")
endif()
