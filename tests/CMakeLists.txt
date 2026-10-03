add_executable(erasure_tests erasure_tests.cpp)
target_link_libraries(erasure_tests PRIVATE lutils::erasure lutils_development)
set_target_properties(erasure_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME erasure.behavior COMMAND erasure_tests)

add_executable(erasure_allocations allocation_tests.cpp)
target_link_libraries(erasure_allocations PRIVATE lutils::erasure lutils_development)
set_target_properties(erasure_allocations PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME erasure.allocations COMMAND erasure_allocations)

add_executable(erasure_translation_units odr_consumer.cpp odr_producer.cpp)
target_link_libraries(erasure_translation_units PRIVATE lutils::erasure lutils_development)
set_target_properties(erasure_translation_units PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME erasure.translation_units COMMAND erasure_translation_units)

foreach(case IN ITEMS empty-owner empty-borrow noexcept-call)
    add_test(NAME erasure.contract.${case}
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:erasure_tests> -DCASE=${case}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_termination.cmake")
endforeach()

foreach(case IN ITEMS cycle missing_member const_mutation copy_move_only)
    add_test(NAME erasure.reject.${case}
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=${CMAKE_CXX_COMPILER}
            -DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}
            -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/erasure/include
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${case}.cpp
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${case}.o
            -DCASE=${case}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endforeach()

add_executable(image_tests image_tests.cpp)
target_link_libraries(image_tests PRIVATE lutils::image lutils_development)
add_test(NAME image.contracts COMMAND image_tests)

if(LUTILS_BUILD_KERNELC)
    lutils_add_kernel(lutils_add_test_kernel NAME add SOURCE compute/add_kernel.cpp ENTRY lutils_test::add)
    lutils_add_kernel(lutils_grouped_test_kernel NAME grouped_add SOURCE compute/add_kernel.cpp ENTRY lutils_test::add LOCAL_SIZE_X 4)
    add_executable(compute_tests compute/integration.cpp)
    target_link_libraries(compute_tests PRIVATE lutils::image_ops lutils::compute_cpu lutils_add_test_kernel lutils_grouped_test_kernel lutils_development)
    add_test(NAME compute.cpu COMMAND compute_tests)
    add_executable(compute_async_tests compute/async.cpp)
    target_link_libraries(compute_async_tests PRIVATE lutils::image_ops lutils::compute_cpu lutils_add_test_kernel lutils_development)
    add_test(NAME compute.async.cpu COMMAND compute_async_tests)
    add_executable(uyvy_nv12_test compute/raw_uyvy.cpp)
    target_link_libraries(uyvy_nv12_test PRIVATE lutils::image_ops lutils::compute_cpu lutils_development)
    if(LUTILS_ENABLE_VULKAN)
        target_link_libraries(compute_tests PRIVATE lutils::compute_vulkan)
        target_compile_definitions(compute_tests PRIVATE LUTILS_TEST_VULKAN)
        add_test(NAME compute.vulkan COMMAND compute_tests vulkan)
        add_test(NAME compute.vulkan.device_local COMMAND compute_tests vulkan --device-local)
        target_link_libraries(compute_async_tests PRIVATE lutils::compute_vulkan)
        target_compile_definitions(compute_async_tests PRIVATE LUTILS_TEST_VULKAN)
        add_test(NAME compute.async.vulkan COMMAND compute_async_tests vulkan)
        target_link_libraries(uyvy_nv12_test PRIVATE lutils::compute_vulkan)
        target_compile_definitions(uyvy_nv12_test PRIVATE LUTILS_TEST_VULKAN)
    endif()
endif()

if(LUTILS_BUILD_KERNELC)
    set(rejection_patterns "unsupported kernel type" "recursive calls" "parameter fields" "aggregate" "volatile"
        "nested mutation" "nested mutation" "unsupported kernel type" "locals require")
    foreach(case RANGE 1 9)
        math(EXPR index "${case} - 1")
        list(GET rejection_patterns ${index} pattern)
        add_test(NAME kernelc.reject.${case} COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:lutils-kernelc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compute/reject.cpp
            -DINCLUDE_DIR=${LUTILS_KERNEL_INCLUDE_DIR} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/rejected_${case}
            -DCASE=${case} "-DPATTERN=${pattern}" -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/reject.cmake)
    endforeach()
    lutils_add_kernel(lutils_float_test_kernel NAME float_kernel SOURCE compute/float_kernel.cpp ENTRY float_test::run)
    target_link_libraries(compute_tests PRIVATE lutils_float_test_kernel)
endif()
if(LUTILS_BUILD_KERNELC)
    add_test(NAME kernelc.dependencies COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:lutils-kernelc> -DROOT=${PROJECT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/compute -DWORK=${CMAKE_CURRENT_BINARY_DIR}/dependency-check
        -DGLSLANG=${LUTILS_GLSLANG} -DSPIRV_VAL=${LUTILS_SPIRV_VAL}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/dependency.cmake)
    add_test(NAME kernelc.output_collision COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:lutils-kernelc> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/collision-check
        -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/output_collision.cmake)
    add_test(NAME kernelc.workgroup_options COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:lutils-kernelc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compute/add_kernel.cpp
        -DINCLUDE_DIR=${LUTILS_KERNEL_INCLUDE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/workgroup-check
        -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/workgroup_options.cmake)
endif()
