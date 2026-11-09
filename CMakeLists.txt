cmake_minimum_required(VERSION 3.25)
project(Velora VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/VeloraOptions.cmake)
include(cmake/VeloraDependencies.cmake)
include(cmake/VeloraSanitizers.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

configure_file(cmake/environment.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/velora/environment.hpp" @ONLY)

add_subdirectory(modules)
if(VELORA_BUILD_TESTS)
    include(cmake/VeloraTests.cmake)
endif()

# Examples: N04 Application startup and the control gallery, both with compiled XAML
if(WIN32 AND VELORA_ENABLE_ANTLR)
    add_subdirectory(examples/n04_startup)
    add_subdirectory(examples/gallery)
endif()

include(cmake/VeloraInstall.cmake)
