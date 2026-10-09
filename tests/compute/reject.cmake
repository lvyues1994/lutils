if(NOT ENTRY)
    set(ENTRY reject_test::run)
endif()
execute_process(COMMAND "${TOOL}" "${SOURCE}" --entry=${ENTRY} --name=reject --output=${OUTPUT}
    -- -std=c++17 "-I${INCLUDE_DIR}" "-DCASE=${CASE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0)
    message(FATAL_ERROR "kernelc incorrectly accepted case ${CASE}")
endif()
if(NOT err MATCHES "${PATTERN}")
    message(FATAL_ERROR "wrong failure for case ${CASE}: ${err}")
endif()
