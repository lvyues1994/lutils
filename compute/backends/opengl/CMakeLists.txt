if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    message(FATAL_ERROR "The EGL compute backend currently supports Linux and Android")
endif()
find_library(LUTILS_EGL_LIBRARY EGL REQUIRED)
find_package(Threads REQUIRED)
find_path(LUTILS_EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
find_path(LUTILS_GLES_INCLUDE_DIR GLES3/gl32.h REQUIRED)
if(NOT TARGET lutils::egl)
    add_library(lutils::egl UNKNOWN IMPORTED GLOBAL)
    set_target_properties(lutils::egl PROPERTIES IMPORTED_LOCATION "${LUTILS_EGL_LIBRARY}")
endif()
add_library(lutils_compute_opengl STATIC Context.cpp Resources.cpp OpenGl.cpp)
add_library(lutils::compute_opengl ALIAS lutils_compute_opengl)
target_include_directories(lutils_compute_opengl PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${LUTILS_EGL_INCLUDE_DIR} ${LUTILS_GLES_INCLUDE_DIR})
target_link_libraries(lutils_compute_opengl PUBLIC lutils::compute
    PRIVATE lutils::egl Threads::Threads "$<BUILD_INTERFACE:lutils_development>")
