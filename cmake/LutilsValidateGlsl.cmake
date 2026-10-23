# Empty variants carry an explicit Unsupported reason in KernelSource.
file(SIZE "${SHADER}" bytes)
if(bytes GREATER 0)
    execute_process(COMMAND "${VALIDATOR}" -S comp "${SHADER}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "GLSL validation failed: ${SHADER}\n${output}${error}")
    endif()
endif()
