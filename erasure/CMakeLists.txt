add_library(lutils_erasure INTERFACE)
add_library(lutils::erasure ALIAS lutils_erasure)
target_compile_features(lutils_erasure INTERFACE cxx_std_17)
target_include_directories(lutils_erasure INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
