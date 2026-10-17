add_executable(simd_uyvy_benchmark simd_uyvy.cpp)
target_link_libraries(simd_uyvy_benchmark PRIVATE lutils::image_simd lutils_development)
if(LUTILS_BUILD_IMAGE_OPS)
    add_executable(uyvy_nv12_benchmark uyvy_nv12.cpp)
    target_link_libraries(uyvy_nv12_benchmark PRIVATE lutils::image_ops lutils::compute_cpu lutils_development)
    target_compile_definitions(uyvy_nv12_benchmark PRIVATE
        LUTILS_BENCHMARK_BUILD="$<CONFIG>"
        LUTILS_BENCHMARK_LOCAL_SIZE_X=${LUTILS_IMAGE_LOCAL_SIZE_X})
    if(LUTILS_ENABLE_VULKAN)
        target_link_libraries(uyvy_nv12_benchmark PRIVATE lutils::compute_vulkan)
        target_compile_definitions(uyvy_nv12_benchmark PRIVATE LUTILS_BENCHMARK_VULKAN=1)
    endif()
endif()
add_subdirectory(simd)
