add_library(lutils_compute STATIC src/Runtime.cpp)
add_library(lutils::compute ALIAS lutils_compute)
target_include_directories(lutils_compute PUBLIC include)
target_link_libraries(lutils_compute PUBLIC lutils::core lutils::compute_kernel PRIVATE lutils_development)
