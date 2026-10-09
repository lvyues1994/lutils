add_library(lutils_compute_kernel INTERFACE)
add_library(lutils::compute_kernel ALIAS lutils_compute_kernel)
target_include_directories(lutils_compute_kernel INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(lutils_compute_kernel INTERFACE cxx_std_17)
set_property(TARGET lutils_compute_kernel PROPERTY LUTILS_SDK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
