include_guard(GLOBAL)

install(FILES "${PROJECT_SOURCE_DIR}/third_party/winui2.3/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Velora" RENAME WinUI2.3-LICENSE.txt)

# Every module library (velora_module) and its headers; plus the build helpers and velorac.
get_property(_targets GLOBAL PROPERTY VELORA_MODULE_TARGETS)
install(TARGETS ${_targets} EXPORT VeloraTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${PROJECT_BINARY_DIR}/generated/velora/environment.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/velora")
install(EXPORT VeloraTargets FILE VeloraTargets.cmake NAMESPACE velora::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Velora")
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/VeloraConfig.cmake.in
    "${PROJECT_BINARY_DIR}/VeloraConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Velora")
write_basic_package_version_file("${PROJECT_BINARY_DIR}/VeloraConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)
install(FILES "${PROJECT_BINARY_DIR}/VeloraConfig.cmake" "${PROJECT_BINARY_DIR}/VeloraConfigVersion.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/VeloraReflect.cmake" "${PROJECT_SOURCE_DIR}/cmake/VeloraXaml.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Velora")
