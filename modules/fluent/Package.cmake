# Velora.Fluent.vpak (R11.1): the WinUI dictionaries compiled by velorac to .vxbf and packed under
# Themes/. Included from modules/CMakeLists.txt after markup, where the velorac target exists.
if(TARGET velorac)
    set(_sources "${PROJECT_SOURCE_DIR}/modules/fluent/resources/WinUI2.3")
    set(_compiled "${PROJECT_BINARY_DIR}/modules/fluent/compiled")
    set(_stage "${PROJECT_BINARY_DIR}/modules/fluent/package")
    set(_staged "")
    foreach(_dictionary IN ITEMS
            SystemResources
            WinUICornerRadius
            WinUIButton
            WinUITextBox
            PersonPictureResources
            FlyoutButtonResources
            MenuFlyoutResources)
        add_custom_command(
            OUTPUT "${_stage}/Themes/${_dictionary}.vxbf"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_compiled}" "${_stage}/Themes"
            COMMAND "$<TARGET_FILE:velorac>" vxbf "${_sources}/${_dictionary}.xaml" "${_sources}" "${_compiled}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${_compiled}/${_dictionary}.vxbf" "${_stage}/Themes/${_dictionary}.vxbf"
            DEPENDS "${_sources}/${_dictionary}.xaml" velorac
            COMMENT "velorac vxbf ${_dictionary}.xaml (Fluent)"
            VERBATIM)
        list(APPEND _staged "${_stage}/Themes/${_dictionary}.vxbf")
    endforeach()
    foreach(_control IN ITEMS PersonPicture NumberBox Flyout DropDownButton SplitButton MenuFlyoutItem MenuFlyoutSeparator MenuBar MenuBarItem AppBarButton)
        add_custom_command(
            OUTPUT "${_stage}/Controls/${_control}View.vxbf"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stage}/Controls"
            COMMAND "$<TARGET_FILE:velorac>" vxbf
                "${PROJECT_SOURCE_DIR}/modules/fluent/resources/Controls/${_control}View.xaml"
                "${PROJECT_SOURCE_DIR}/modules/fluent/resources" "${_compiled}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${_compiled}/Controls/${_control}View.vxbf"
                "${_stage}/Controls/${_control}View.vxbf"
            DEPENDS "${PROJECT_SOURCE_DIR}/modules/fluent/resources/Controls/${_control}View.xaml" velorac
            VERBATIM)
        list(APPEND _staged "${_stage}/Controls/${_control}View.vxbf")
    endforeach()
    add_custom_command(
        OUTPUT "${_stage}/Licenses/WinUI2.3-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stage}/Licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/modules/fluent/LICENSE-WinUI2.3.txt"
            "${_stage}/Licenses/WinUI2.3-LICENSE.txt"
        DEPENDS "${PROJECT_SOURCE_DIR}/modules/fluent/LICENSE-WinUI2.3.txt" VERBATIM)
    list(APPEND _staged "${_stage}/Licenses/WinUI2.3-LICENSE.txt")
    set(VELORA_FLUENT_PACKAGE "${PROJECT_BINARY_DIR}/modules/fluent/Velora.Fluent.vpak" CACHE INTERNAL "Fluent resource package")
    add_custom_command(
        OUTPUT "${VELORA_FLUENT_PACKAGE}"
        COMMAND "$<TARGET_FILE:velorac>" pack --name Velora.Fluent --version 2.3.0 --compress "${_stage}"
            "${VELORA_FLUENT_PACKAGE}"
        DEPENDS ${_staged} velorac
        COMMENT "velorac pack Velora.Fluent.vpak"
        VERBATIM)
    add_custom_target(velora_fluent_package DEPENDS "${VELORA_FLUENT_PACKAGE}")
    install(FILES "${VELORA_FLUENT_PACKAGE}" OPTIONAL
        DESTINATION "${CMAKE_INSTALL_DATADIR}/velora/Resources")
endif()
