add_library(lutils_image_simd STATIC src/Conversion.cpp src/Scalar.cpp src/Tuning.cpp)
add_library(lutils::image_simd ALIAS lutils_image_simd)
target_include_directories(lutils_image_simd PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_image_simd PUBLIC lutils::image lutils::simd_dispatch
    PRIVATE "$<BUILD_INTERFACE:lutils_development>")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_sources(lutils_image_simd PRIVATE src/Sse2.cpp src/Avx2.cpp)
    target_compile_definitions(lutils_image_simd PRIVATE LUTILS_SIMD_X86)
    set_source_files_properties(src/Sse2.cpp PROPERTIES COMPILE_OPTIONS "-msse2;-mno-avx;-mno-fma")
    set_source_files_properties(src/Avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mno-fma")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    target_sources(lutils_image_simd PRIVATE src/Neon.cpp)
    target_compile_definitions(lutils_image_simd PRIVATE LUTILS_SIMD_ARM64)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lutils_image_simd PRIVATE -ffp-contract=off)
endif()
