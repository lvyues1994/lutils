file(MAKE_DIRECTORY "${WORK}")
foreach(value IN ITEMS 0 -1 4294967296)
    execute_process(COMMAND "${TOOL}" "${SOURCE}" --entry=lutils_test::add --name=invalid
        "--output=${WORK}/invalid" "--local-size-x=${value}" -- -std=c++17 "-I${INCLUDE_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "local-size-x")
        message(FATAL_ERROR "Invalid workgroup size ${value} was not rejected: ${out}\n${err}")
    endif()
endforeach()
