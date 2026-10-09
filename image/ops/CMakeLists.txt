set(LUTILS_IMAGE_LOCAL_SIZE_X 32 CACHE STRING "Image kernel workgroup width")
lutils_add_shader(lutils_packed422_kernel OBJECT NAME packed422_to_nv12 SOURCE kernels/Conversions.cpp ENTRY lutils::image::kernels::Packed422ToNv12 DEFINITIONS LUTILS_IMAGE_LOCAL_SIZE_X=${LUTILS_IMAGE_LOCAL_SIZE_X})
lutils_add_shader(lutils_rgb_kernel OBJECT NAME nv12_to_rgba SOURCE kernels/Conversions.cpp ENTRY lutils::image::kernels::Nv12ToRgba DEFINITIONS LUTILS_IMAGE_LOCAL_SIZE_X=${LUTILS_IMAGE_LOCAL_SIZE_X})
add_library(lutils_image_ops STATIC src/Conversion.cpp)
add_library(lutils::image_ops ALIAS lutils_image_ops)
target_include_directories(lutils_image_ops PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(lutils_image_ops PUBLIC lutils::image lutils::compute PRIVATE "$<BUILD_INTERFACE:lutils_packed422_kernel>" "$<BUILD_INTERFACE:lutils_rgb_kernel>" "$<BUILD_INTERFACE:lutils_development>")
