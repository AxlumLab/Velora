cmake_minimum_required(VERSION 3.20)

# N04 startup example: WinUI-like Application with compiled XAML
add_executable(n04_startup WIN32
    main.cpp
    MainWindow.cpp
)

# Compiled XAML through the .vxbf pipeline (R07.6): velorac writes MainWindow.vxbf and a
# code-behind that loads it with velora::loadComponent; no generated code builds the tree.
velora_add_xaml(n04_startup
    XAML MainWindow.xaml
    HEADERS MainWindow.hpp
    BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(n04_startup PRIVATE
    velora::application
    velora::controls
    velora::graphics
)

target_compile_definitions(n04_startup PRIVATE UNICODE _UNICODE NOMINMAX)

# Set as startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT n04_startup)
