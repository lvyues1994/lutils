cmake_minimum_required(VERSION 3.21)
project(lutils VERSION 0.1.0 LANGUAGES CXX)

set(lutils_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(lutils_top_level ON)
endif()

option(LUTILS_BUILD_TESTS "Build lutils tests" ${lutils_top_level})
option(LUTILS_BUILD_EXAMPLES "Build lutils examples" ${lutils_top_level})
option(LUTILS_ENABLE_SANITIZERS "Instrument lutils tests and examples" OFF)

add_subdirectory(erasure)

if(LUTILS_BUILD_TESTS OR LUTILS_BUILD_EXAMPLES)
    add_library(lutils_development INTERFACE)
    target_compile_features(lutils_development INTERFACE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lutils_development INTERFACE
            -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion
            -Wold-style-cast -Wnon-virtual-dtor)
        if(LUTILS_ENABLE_SANITIZERS)
            target_compile_options(lutils_development INTERFACE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(lutils_development INTERFACE
                -fsanitize=address,undefined)
        endif()
    elseif(MSVC)
        target_compile_options(lutils_development INTERFACE /W4 /WX /permissive-)
    endif()
endif()

if(LUTILS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(LUTILS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
