# Fluent resources (R11.1): the WinUI 2.3 theme dictionaries, default styles and control templates
# shipped as the framework resource package Velora.Fluent.vpak, overridable by the application.
#
# `velora::fluent` holds resources, not behavior: no control C++ belongs here, so an application can
# restyle every default without touching compiled control code. The library only mounts the
# package (mountFluent).
#
# The MIT-licensed WinUI 2.3 excerpts live in resources/WinUI2.3.
# Runtime package compilation and mounting remain separate from control implementation.
velora_module(fluent
    SOURCES velora/fluent/FluentPackage.cpp
    PUBLIC velora::resources)
# The package itself is built by Package.cmake once velorac exists (modules/CMakeLists.txt).
