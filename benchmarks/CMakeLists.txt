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
if(LUTILS_BUILD_IMAGE_OPS AND LUTILS_ENABLE_OPENGL AND NOT ANDROID)
    add_executable(opengl_readback_benchmark opengl_readback.cpp)
    target_link_libraries(opengl_readback_benchmark PRIVATE
        lutils_camera_rays lutils_sphere_tracer lutils_visualize_rays
        lutils::compute_cpu lutils::compute_opengl lutils_development)
    set_target_properties(opengl_readback_benchmark PROPERTIES CXX_EXTENSIONS OFF)
endif()
