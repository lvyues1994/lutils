add_library(lutils_core INTERFACE)
add_library(lutils::core ALIAS lutils_core)
target_include_directories(lutils_core INTERFACE include)
target_compile_features(lutils_core INTERFACE cxx_std_17)
