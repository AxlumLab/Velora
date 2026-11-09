# Platform HAL (Win32): windowing, the UI thread dispatcher, input, IME and the surface
# descriptor. The platform layer reports events and handles; it never routes to controls,
# lays out or resolves focus.
velora_module(platform
    SOURCES
        velora/platform/windowing/windows/NativeWindow.cpp
        velora/platform/windowing/windows/WindowProc.cpp
        velora/platform/dispatcher/windows/Dispatcher.cpp
        velora/platform/ime/windows/TsfTextInput.cpp
    PUBLIC velora::foundation
    PRIVATE user32 imm32 shell32 ole32 uuid)
