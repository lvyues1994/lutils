add_library(lutils_compute_cpu STATIC Cpu.cpp)
add_library(lutils::compute_cpu ALIAS lutils_compute_cpu)
target_link_libraries(lutils_compute_cpu PUBLIC lutils::compute PRIVATE "$<BUILD_INTERFACE:lutils_development>")
