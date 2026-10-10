include(CMakePackageConfigHelpers)

set(lutils_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/lutils")
set(lutils_base_targets lutils_core lutils_erasure lutils_image lutils_compute_kernel
    lutils_compute lutils_compute_cpu lutils_image_memory)
foreach(target IN LISTS lutils_base_targets)
    string(REGEX REPLACE "^lutils_" "" export_name "${target}")
    set_target_properties(${target} PROPERTIES EXPORT_NAME "${export_name}")
endforeach()
install(TARGETS ${lutils_base_targets} EXPORT lutilsCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT lutilsCoreTargets NAMESPACE lutils:: DESTINATION ${lutils_package_dir})
foreach(module IN ITEMS core erasure image image/memory compute/kernel compute/runtime)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/${module}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endforeach()
if(LUTILS_ENABLE_VULKAN)
    set_target_properties(lutils_compute_vulkan PROPERTIES EXPORT_NAME compute_vulkan)
    install(TARGETS lutils_compute_vulkan EXPORT lutilsVulkanTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT lutilsVulkanTargets NAMESPACE lutils:: DESTINATION ${lutils_package_dir})
    install(DIRECTORY compute/backends/vulkan/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(LUTILS_BUILD_IMAGE_OPS)
    set_target_properties(lutils_image_ops PROPERTIES EXPORT_NAME image_ops)
    install(TARGETS lutils_image_ops EXPORT lutilsImageOpsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT lutilsImageOpsTargets NAMESPACE lutils:: DESTINATION ${lutils_package_dir})
    install(DIRECTORY image/ops/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(LUTILS_BUILD_KERNELC)
    set_target_properties(lutils-kernelc PROPERTIES EXPORT_NAME kernelc)
    install(TARGETS lutils-kernelc EXPORT lutilsToolsTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT lutilsToolsTargets NAMESPACE lutils:: DESTINATION ${lutils_package_dir})
endif()
if(TARGET lutils::kernelc)
    install(FILES cmake/LutilsKernel.cmake DESTINATION ${lutils_package_dir})
endif()
configure_package_config_file(cmake/lutilsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/lutilsConfig.cmake"
    INSTALL_DESTINATION ${lutils_package_dir} PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/lutilsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lutilsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/lutilsConfigVersion.cmake" DESTINATION ${lutils_package_dir})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples DESTINATION ${CMAKE_INSTALL_DOCDIR}
    FILES_MATCHING PATTERN "*.cpp" PATTERN "*.hpp" PATTERN "CMakeLists.txt")
install(FILES tests/compute/add_kernel.cpp tests/compute/workgroup.hpp tests/compute/half_kernel.hpp
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/tests/compute)
if(lutils_top_level)
    set(CPACK_PACKAGE_NAME lutils)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_GENERATOR TGZ)
    set(CPACK_SOURCE_GENERATOR TGZ)
    set(CPACK_SOURCE_PACKAGE_FILE_NAME "lutils-${PROJECT_VERSION}-source")
    set(lutils_source_regex "${PROJECT_SOURCE_DIR}")
    foreach(meta IN ITEMS "\\" "." "^" "$" "*" "+" "?" "(" ")" "[" "]" "{" "}" "|")
        string(REPLACE "${meta}" "\\${meta}" lutils_source_regex "${lutils_source_regex}")
    endforeach()
    set(CPACK_VERBATIM_VARIABLES YES)
    set(CPACK_SOURCE_IGNORE_FILES
        "^${lutils_source_regex}/(build|\\.git|\\.codex|\\.agents)(/|$);^${lutils_source_regex}/CMakeUserPresets\\.json$;~$")
    include(CPack)
endif()
