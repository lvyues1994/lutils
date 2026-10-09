if(COMPILER_ID STREQUAL "MSVC")
    execute_process(COMMAND "${COMPILER}" /nologo /std:c++17 /EHsc
        "/I${INCLUDE_DIR}" "/DREJECT_CASE=${CASE}" /c "${SOURCE}" "/Fo${OUTPUT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
else()
    execute_process(COMMAND "${COMPILER}" -std=c++17 -pedantic-errors
        "-I${INCLUDE_DIR}" "-DREJECT_CASE=${CASE}" -fsyntax-only "${SOURCE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
endif()
if("${result}" STREQUAL "0")
    message(FATAL_ERROR "expected case ${CASE}: compilation unexpectedly succeeded")
endif()
if(NOT "${out}${err}" MATCHES "${PATTERN}" OR
   "${out}${err}" MATCHES "internal compiler error|PLEASE submit a bug report|file not found|No such file")
    message(FATAL_ERROR "expected case ${CASE}: unexpected failure:\n${out}${err}")
endif()
