# Internal Fluent controls and their compiled resources. WinUI 2.3 behavior is incorporated here
# as ordinary Velora C++ controls; controls supplies the shared UI infrastructure.
# porting/ contains staged migration inputs only; never glob it into this target or the SDK.
velora_module(fluent
    SOURCES velora/fluent/FluentPackage.cpp
        velora/fluent/InitialsGenerator.cpp
        velora/fluent/PersonPicture.cpp
        velora/fluent/NumberBox.cpp
        velora/fluent/NumberBoxParser.cpp
        velora/fluent/Flyout.cpp
        velora/fluent/FlyoutButton.cpp
        velora/fluent/MenuFlyout.cpp
        velora/fluent/Icons.cpp
        velora/fluent/MenuBar.cpp
        velora/fluent/AppBarButton.cpp
        velora/fluent/CommandBar.cpp
        velora/fluent/PaneLayout.cpp
        velora/fluent/SplitView.cpp
        velora/fluent/TwoPaneView.cpp
        velora/fluent/RatingControl.cpp
        velora/fluent/ToolTip.cpp
    PUBLIC velora::controls velora::resources)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/FluentControls.vtypes" FLUENT_TYPES)
configure_file(FluentTypes.hpp.in "${PROJECT_BINARY_DIR}/generated/fluent/FluentTypes.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/FluentControls.vtypes")
target_include_directories(velora_fluent PRIVATE "${PROJECT_BINARY_DIR}/generated/fluent")
# The package itself is built by Package.cmake once velorac exists (modules/CMakeLists.txt).
