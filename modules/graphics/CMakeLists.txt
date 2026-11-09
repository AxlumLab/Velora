# Public graphics module, including text layout and font services.
# Windows: Direct2D, DirectWrite and DXGI. Other platform backends remain placeholders.
set(_graphics_sources
    velora/text/TextService.cpp)
if(WIN32)
    list(APPEND _graphics_sources
        velora/graphics/windows/D2DDevice.cpp
        velora/graphics/windows/D2DSurface.cpp
        velora/graphics/windows/D2DPlayback.cpp
        velora/graphics/windows/D2DResources.cpp
        velora/text/windows/DWriteText.cpp)
else()
    list(APPEND _graphics_sources velora/graphics/Graphics.cpp)
endif()
velora_module(graphics SOURCES ${_graphics_sources} PUBLIC velora::foundation)
if(WIN32)
    target_link_libraries(velora_graphics PRIVATE d2d1 d3d11 dxgi dxguid windowscodecs dwrite)
endif()
