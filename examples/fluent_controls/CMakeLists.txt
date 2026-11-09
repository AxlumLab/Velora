add_executable(velora_fluent_gallery WIN32 main.cpp)
velora_add_xaml(velora_fluent_gallery XAML FluentPage.xaml HEADERS FluentPage.hpp
    TYPES "${PROJECT_SOURCE_DIR}/modules/fluent/resources/FluentControls.vtypes"
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velora_fluent_gallery PRIVATE velora::application velora::fluent)
target_compile_definitions(velora_fluent_gallery PRIVATE UNICODE _UNICODE NOMINMAX)
add_dependencies(velora_fluent_gallery velora_fluent_package)
add_custom_command(TARGET velora_fluent_gallery POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VELORA_FLUENT_PACKAGE}"
        "$<TARGET_FILE_DIR:velora_fluent_gallery>/Velora.Fluent.vpak"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_SOURCE_DIR}/modules/fluent/LICENSE-WinUI2.3.txt"
        "$<TARGET_FILE_DIR:velora_fluent_gallery>/WinUI2.3-LICENSE.txt" VERBATIM)
