add_library(lutils_simd INTERFACE)
add_library(lutils::simd ALIAS lutils_simd)
target_include_directories(lutils_simd INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(lutils_simd INTERFACE cxx_std_17)
target_compile_options(lutils_simd INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-ffp-contract=off>")

add_library(lutils_simd_dispatch STATIC src/Dispatch.cpp)
add_library(lutils::simd_dispatch ALIAS lutils_simd_dispatch)
target_link_libraries(lutils_simd_dispatch PUBLIC lutils::simd
    PRIVATE "$<BUILD_INTERFACE:lutils_development>")
