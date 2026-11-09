# Dependency-ordered modules; each is one library, velora::<module>.
include(${PROJECT_SOURCE_DIR}/cmake/VeloraModule.cmake)
add_subdirectory(foundation)
add_subdirectory(reflect)
add_subdirectory(resources)
add_subdirectory(graphics)
add_subdirectory(composition)
add_subdirectory(runtime)
# XAML format/loading and retained controls, built on the runtime property system.
add_subdirectory(xaml)
add_subdirectory(controls)
add_subdirectory(fluent)
if(WIN32)
    add_subdirectory(platform)  # the native platform layer exists for Windows 10 only
endif()
add_subdirectory(application)
if(VELORA_ENABLE_ANTLR)
    add_subdirectory(markup)    # build-time compilers: velorac and its libraries
    include(fluent/Package.cmake) # Velora.Fluent.vpak, compiled by velorac
endif()
