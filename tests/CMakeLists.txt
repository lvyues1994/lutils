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
