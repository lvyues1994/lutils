add_library(lutils_image_memory STATIC src/ImageResource.cpp)
add_library(lutils::image_memory ALIAS lutils_image_memory)
target_include_directories(lutils_image_memory PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_image_memory PUBLIC lutils::image lutils::compute
    PRIVATE "$<BUILD_INTERFACE:lutils_development>")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(lutils_image_memory PRIVATE src/LinuxImage.cpp)
endif()
