cmake_minimum_required(VERSION 3.20)

# A compact gallery exercising the integrated core controls and compiled XAML bindings.
add_executable(velora_gallery WIN32
    main.cpp
    Gallery.cpp
)

velora_add_xaml(velora_gallery
    XAML GalleryPage.xaml
    HEADERS Gallery.hpp
    REFLECT Gallery.hpp
    BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(velora_gallery PRIVATE
    velora::application
    velora::controls
    velora::graphics
)

target_compile_definitions(velora_gallery PRIVATE UNICODE _UNICODE NOMINMAX)
