find_package(Threads REQUIRED)
add_library(lutils_compute STATIC src/Runtime.cpp src/CpuExecution.cpp)
add_library(lutils::compute ALIAS lutils_compute)
target_include_directories(lutils_compute PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_compute PUBLIC lutils::core lutils::compute_kernel Threads::Threads PRIVATE "$<BUILD_INTERFACE:lutils_development>")
