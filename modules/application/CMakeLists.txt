# Application lifecycle, windows and the XAML host.
velora_module(application
    SOURCES velora/application/Application.cpp velora/application/AppWindow.cpp
        velora/application/XamlHost.cpp velora/application/NativeSink.cpp
        velora/application/XamlApplication.cpp velora/application/XamlWindow.cpp
        velora/application/XamlRoot.cpp
    PUBLIC velora::foundation velora::controls velora::resources
    PRIVATE velora::graphics  velora::composition)
if(WIN32)
    target_link_libraries(velora_application PRIVATE velora::platform)
endif()
