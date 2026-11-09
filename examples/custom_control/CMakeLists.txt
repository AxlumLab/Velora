add_executable(velora_custom_control WIN32 main.cpp ValuePad.cpp)
include(Types.cmake)
target_include_directories(velora_custom_control PRIVATE "${PROJECT_BINARY_DIR}/generated/examples")
velora_add_xaml(velora_custom_control XAML SourcePage.xaml HEADERS SourcePage.hpp
    TYPES ValuePad.vtypes BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velora_custom_control PRIVATE velora::application velora::controls)
target_compile_definitions(velora_custom_control PRIVATE UNICODE _UNICODE NOMINMAX)
