foreach(case IN ITEMS traits behavior exceptions)
    add_executable(expected_${case}_tests expected/${case}.cpp)
    target_link_libraries(expected_${case}_tests PRIVATE lutils::core lutils_development)
    set_target_properties(expected_${case}_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    add_test(NAME expected.${case} COMMAND expected_${case}_tests)
endforeach()

set(expected_rejections
    "object or void value type" "unqualified object error type"
    "and_then must preserve error_type" "or_else must preserve value_type"
    "transform must return an object or void" "transform_error must return an unqualified object"
    "value\\(\\) requires a copyable error" "emplace"
    "unexpected requires an unqualified object" "and_then must return lutils::expected"
    "object or void value type" "deleted|C2280")
foreach(case RANGE 1 12)
    math(EXPR index "${case} - 1")
    list(GET expected_rejections ${index} pattern)
    add_test(NAME expected.reject.${case} COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=${CMAKE_CXX_COMPILER} -DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}
        -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/core/include
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/expected/reject.cpp
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/expected-reject-${case}.o
        -DCASE=${case} "-DPATTERN=${pattern}" -P ${CMAKE_CURRENT_SOURCE_DIR}/expected/reject.cmake)
endforeach()

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

if(LUTILS_INSTALL AND lutils_top_level)
    add_test(NAME downstream.packages COMMAND ${CMAKE_COMMAND}
        -DROOT=${PROJECT_SOURCE_DIR} -DBUILD=${PROJECT_BINARY_DIR}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/package-check -DCOMPILER=${CMAKE_CXX_COMPILER}
        -DFULL=${LUTILS_BUILD_KERNELC} -DGPU=${LUTILS_ENABLE_VULKAN}
        -DLLVM_DIR=${LLVM_DIR} -DCLANG_INCLUDE=${LUTILS_CLANG_INCLUDE_DIR}
        -DCLANG_LIBRARY=${LUTILS_CLANG_CPP_LIBRARY}
        -DGLSLANG=${LUTILS_GLSLANG} -DSPIRV_VAL=${LUTILS_SPIRV_VAL}
        -DCPACK=${CMAKE_CPACK_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/downstream/package.cmake)
    set_tests_properties(downstream.packages PROPERTIES TIMEOUT 300 LABELS downstream)
endif()

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
add_executable(cpu_execution_tests compute/execution.cpp)
target_link_libraries(cpu_execution_tests PRIVATE lutils::compute lutils_development)
add_test(NAME compute.execution COMMAND cpu_execution_tests)
set_tests_properties(compute.execution PROPERTIES TIMEOUT 30)

foreach(mode IN ITEMS source install)
    if(mode STREQUAL "install" AND NOT LUTILS_INSTALL)
        continue()
    endif()
    add_test(NAME downstream.${mode} COMMAND ${CMAKE_COMMAND}
        -DMODE=${mode} -DROOT=${PROJECT_SOURCE_DIR} -DBUILD=${PROJECT_BINARY_DIR}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/downstream-${mode}
        -DCOMPILER=${CMAKE_CXX_COMPILER} -DFULL=${LUTILS_BUILD_KERNELC}
        -DGPU=${LUTILS_ENABLE_VULKAN} -DLLVM_DIR=${LLVM_DIR}
        -DCLANG_INCLUDE=${LUTILS_CLANG_INCLUDE_DIR} -DCLANG_LIBRARY=${LUTILS_CLANG_CPP_LIBRARY}
        -DGLSLANG=${LUTILS_GLSLANG} -DSPIRV_VAL=${LUTILS_SPIRV_VAL}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/downstream/check.cmake)
    set_tests_properties(downstream.${mode} PROPERTIES TIMEOUT 300 LABELS downstream)
endforeach()

if(LUTILS_BUILD_KERNELC)
    lutils_add_kernel(lutils_add_test_kernel NAME add SOURCE compute/add_kernel.cpp ENTRY lutils_test::add)
    lutils_add_kernel(lutils_grouped_test_kernel NAME grouped_add SOURCE compute/add_kernel.cpp ENTRY lutils_test::add LOCAL_SIZE_X 4)
    add_executable(compute_tests compute/integration.cpp)
    target_link_libraries(compute_tests PRIVATE lutils::image_ops lutils::compute_cpu lutils_add_test_kernel lutils_grouped_test_kernel lutils_packed422_kernel lutils_development)
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

if(LUTILS_BUILD_KERNELC)
    add_executable(shader_tests compute/shader.cpp)
    target_link_libraries(shader_tests PRIVATE lutils_typed_add lutils::compute_cpu lutils_development)
    add_test(NAME shader.cpu COMMAND shader_tests)
    if(LUTILS_ENABLE_VULKAN)
        target_link_libraries(shader_tests PRIVATE lutils::compute_vulkan)
        target_compile_definitions(shader_tests PRIVATE LUTILS_TEST_VULKAN)
        add_test(NAME shader.vulkan COMMAND shader_tests vulkan)
    endif()
endif()

if(LUTILS_BUILD_KERNELC)
    add_executable(workgroup_tests compute/workgroup.cpp)
    target_link_libraries(workgroup_tests PRIVATE lutils::compute_cpu lutils_development)
    foreach(pair IN ITEMS "reduction;Reduction" "histogram;Histogram" "atomic_ops;AtomicOps" "cancellation;Cancellation" "global_counter;GlobalCounter")
        list(GET pair 0 name)
        list(GET pair 1 entry)
        lutils_add_shader(lutils_${name} NAME ${name} SOURCE compute/workgroup.hpp ENTRY group_test::${entry})
        target_link_libraries(workgroup_tests PRIVATE lutils_${name})
    endforeach()
    add_test(NAME compute.workgroup.cpu COMMAND workgroup_tests)
    set_tests_properties(compute.workgroup.cpu PROPERTIES TIMEOUT 30)
    if(LUTILS_ENABLE_VULKAN)
        target_link_libraries(workgroup_tests PRIVATE lutils::compute_vulkan)
        target_compile_definitions(workgroup_tests PRIVATE LUTILS_TEST_VULKAN)
        add_test(NAME compute.workgroup.vulkan COMMAND workgroup_tests vulkan)
        set_tests_properties(compute.workgroup.vulkan PROPERTIES TIMEOUT 30)
    endif()
    set(group_patterns "top-level statement" "cannot return early" "top-level statement" "buffer or shared lvalues" "nested mutation" "nested mutation")
    foreach(case RANGE 1 6)
        math(EXPR index "${case} - 1")
        list(GET group_patterns ${index} pattern)
        add_test(NAME shader.group.reject.${case} COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:lutils-kernelc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compute/reject_group.hpp
            -DINCLUDE_DIR=${LUTILS_KERNEL_INCLUDE_DIR} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/group_rejected_${case}
            -DENTRY=group_reject::Kernel -DCASE=${case} "-DPATTERN=${pattern}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/reject.cmake)
    endforeach()
endif()

if(LUTILS_BUILD_KERNELC)
    target_link_libraries(shader_tests PRIVATE lutils_camera_rays)
    target_link_libraries(shader_tests PRIVATE lutils_sphere_tracer)
    target_link_libraries(shader_tests PRIVATE lutils_visualize_rays)
    target_link_libraries(shader_tests PRIVATE lutils_game_of_life)
    target_link_libraries(shader_tests PRIVATE lutils_volume)
    target_link_libraries(shader_tests PRIVATE lutils_line)
endif()

if(LUTILS_BUILD_KERNELC)
    lutils_add_shader(lutils_layout NAME layout SOURCE compute/layout_kernel.hpp ENTRY layout_test::Layout)
    target_link_libraries(shader_tests PRIVATE lutils_layout)
    set(image_formats
        R8 RG8 RGBA8 R8Snorm RG8Snorm RGBA8Snorm R8UI RG8UI RGBA8UI R8I RG8I RGBA8I R16 RG16 RGBA16 R16Snorm RG16Snorm RGBA16Snorm R16UI RG16UI RGBA16UI R16I RG16I RGBA16I R32UI RG32UI RGBA32UI R32I RG32I RGBA32I R32F RG32F RGBA32F R16F RG16F RGBA16F)
    foreach(format IN LISTS image_formats)
        lutils_add_shader(lutils_format_${format} NAME format_${format}
            SOURCE compute/formats.hpp ENTRY format_test::Copy${format})
        target_link_libraries(shader_tests PRIVATE lutils_format_${format})
    endforeach()
endif()

add_executable(shader_value_tests compute/values.cpp)
target_link_libraries(shader_value_tests PRIVATE lutils::compute_cpu lutils_development)
add_test(NAME shader.values COMMAND shader_value_tests)
add_executable(half_value_tests compute/half_values.cpp)
target_link_libraries(half_value_tests PRIVATE lutils::compute_kernel lutils_development)
add_test(NAME shader.half.values COMMAND half_value_tests)
if(LUTILS_BUILD_KERNELC)
    lutils_add_shader(lutils_half_arithmetic NAME half_arithmetic SOURCE compute/half_kernel.hpp ENTRY half_test::Arithmetic)
    lutils_add_shader(lutils_half_shared NAME half_shared SOURCE compute/half_kernel.hpp ENTRY half_test::Shared)
    add_executable(half_tests compute/half.cpp)
    target_link_libraries(half_tests PRIVATE lutils_half_arithmetic lutils_half_shared lutils::compute_cpu lutils_development)
    add_test(NAME shader.half.cpu COMMAND half_tests)
    if(LUTILS_ENABLE_VULKAN)
        target_link_libraries(half_tests PRIVATE lutils::compute_vulkan)
        target_compile_definitions(half_tests PRIVATE LUTILS_TEST_VULKAN)
        add_test(NAME shader.half.vulkan COMMAND half_tests vulkan)
        set_tests_properties(shader.half.vulkan PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
if(LUTILS_BUILD_KERNELC)
    set(shader_rejection_patterns "custom overloaded" "aggregates" "side effects" "positive bounded"
        "dynamic allocation" "uniforms are read only" "builtins are read only" "arrays are read only"
        "recursive shader" "duplicate buffer" "64-bit integer" "nested mutation" "receiver must be this" "reinterpret casts" "swizzle component count")
    foreach(case RANGE 1 15)
        math(EXPR index "${case} - 1")
        list(GET shader_rejection_patterns ${index} pattern)
        add_test(NAME shader.reject.${case} COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:lutils-kernelc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compute/reject_shader.hpp
            -DINCLUDE_DIR=${LUTILS_KERNEL_INCLUDE_DIR} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shader_rejected_${case}
            -DCASE=${case} "-DPATTERN=${pattern}" -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/reject.cmake)
    endforeach()
endif()
if(LUTILS_BUILD_KERNELC)
    add_test(NAME shader.preprocessing_profile COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:lutils-kernelc> -DROOT=${PROJECT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/compute -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shader-profile-check
        -DGLSLANG=${LUTILS_GLSLANG} -DSPIRV_VAL=${LUTILS_SPIRV_VAL}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/compute/shader_profile.cmake)
endif()
