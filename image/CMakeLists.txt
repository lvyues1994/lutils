add_library(lutils_image STATIC src/Frame.cpp)
add_library(lutils::image ALIAS lutils_image)
target_include_directories(lutils_image PUBLIC include)
target_link_libraries(lutils_image PUBLIC lutils::core PRIVATE lutils_development)
