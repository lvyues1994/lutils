find_package(Vulkan REQUIRED)
add_library(lutils_compute_vulkan STATIC Vulkan.cpp)
add_library(lutils::compute_vulkan ALIAS lutils_compute_vulkan)
target_include_directories(lutils_compute_vulkan PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_compute_vulkan PUBLIC lutils::compute PRIVATE Vulkan::Vulkan "$<BUILD_INTERFACE:lutils_development>")
