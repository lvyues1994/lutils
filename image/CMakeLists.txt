add_library(lutils_image STATIC src/Frame.cpp src/Region.cpp)
add_library(lutils::image ALIAS lutils_image)
target_include_directories(lutils_image PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_image PUBLIC lutils::core PRIVATE "$<BUILD_INTERFACE:lutils_development>")
