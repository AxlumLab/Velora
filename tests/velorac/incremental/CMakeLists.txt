# Generation-only project used by run.cmake; it never compiles the generated views.
cmake_minimum_required(VERSION 3.25)
project(velorac_incremental LANGUAGES NONE)
include("${VELORA_CMAKE_DIR}/VeloraReflect.cmake")
include("${VELORA_CMAKE_DIR}/VeloraXaml.cmake")
add_custom_target(views ALL)
velora_xaml_compile(views
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    HEADERS Controllers.hpp
    XAML Main.xaml Chip.xaml Other.xaml
    RESOURCES Colors.xaml)
