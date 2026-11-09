# Standalone consumer of an installed Velora package (N00 gate). It links the HAL scaffolding
# and the retained runtime, and compiles a XAML page with the installed velorac through the
# installed VeloraXaml.cmake, exactly as an application would.
cmake_minimum_required(VERSION 3.25)
project(velora_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Velora CONFIG REQUIRED)
find_program(VELORAC_EXECUTABLE velorac REQUIRED HINTS "${Velora_DIR}/../../../bin")
include("${Velora_DIR}/VeloraReflect.cmake")
include("${Velora_DIR}/VeloraXaml.cmake")

add_executable(velora_consumer main.cpp)
target_link_libraries(velora_consumer PRIVATE velora::application velora::graphics  velora::controls)
velora_xaml_load(velora_consumer
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    HEADERS Hello.hpp
    REFLECT Hello.hpp
    XAML Hello.xaml)
