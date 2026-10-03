execute_process(COMMAND "${PROGRAM}" "${CASE}" RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "42")
    message(FATAL_ERROR "${CASE}: expected terminate handler exit 42, got ${result}")
endif()
