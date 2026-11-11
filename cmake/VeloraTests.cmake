include_guard(GLOBAL)

# Unit-test executables are opt-in, never product entrypoints.
enable_testing()
add_executable(velora_core_types_test tests/core_types_memory_test.cpp)
target_link_libraries(velora_core_types_test PRIVATE velora::foundation)
add_test(NAME velora_core_types_test COMMAND velora_core_types_test)

function(velora_add_core_test name source)
    add_executable(${name} ${source})
    foreach(dependency IN LISTS ARGN)
        target_link_libraries(${name} PRIVATE ${dependency})
    endforeach()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

velora_add_core_test(velora_reflect_consumer tests/reflect_consumer.cpp velora::reflect)
velora_add_core_test(velora_reflect_boundaries tests/reflect_boundaries.cpp velora::reflect)
velora_add_core_test(velora_data_test tests/data_test.cpp velora::foundation)
velora_add_core_test(velora_encoding_test tests/encoding_test.cpp velora::foundation)
velora_add_core_test(velora_element_tree_consumer tests/element_tree_consumer.cpp velora::controls)
velora_add_core_test(velora_composition_consumer tests/composition_consumer.cpp velora::composition)
velora_add_core_test(velora_visual_bridge_test tests/visual_bridge_test.cpp velora::controls)
velora_add_core_test(velora_framework_test tests/framework_test.cpp velora::controls)
velora_add_core_test(velora_scroll_viewer_test tests/scroll_viewer_test.cpp velora::controls velora::foundation)
velora_add_core_test(velora_controls_test tests/controls_test.cpp velora::controls velora::foundation)
velora_add_core_test(velora_hal_skeleton_test tests/hal_skeleton_test.cpp
    velora::application velora::graphics velora::composition)
# R01: resource identity (path normalization, no escape from the package root).
velora_add_core_test(velora_resource_uri_test tests/resource_uri_test.cpp velora::resources)
# R08: qualifier ranking, .vpri index, .vpak container, deflate, loose/packed equivalence, mounting.
velora_add_core_test(velora_resource_package_test tests/resource_package_test.cpp velora::resources)
# Type registration and deterministic type identities (ordinary C++ API).
velora_add_core_test(velora_type_registry_test tests/type_registry_test.cpp velora::runtime)
# R03: dependency properties — precedence, expressions, notification safety, the ElementTree bridge.
velora_add_core_test(velora_dependency_property_test tests/dependency_property_test.cpp velora::controls)
# R05: typed controls, object-first creation, layout overrides, routed events, type hierarchy.
velora_add_core_test(velora_controls_core_test tests/controls_core_test.cpp velora::controls)
add_executable(velora_fluent_controls_test tests/fluent_controls_test.cpp)
add_executable(velora_fluent_buttons_test tests/fluent_buttons_test.cpp)
target_link_libraries(velora_fluent_buttons_test PRIVATE velora::fluent)
target_link_libraries(velora_fluent_controls_test PRIVATE velora::fluent)
set(VELORA_FLUENT_TEST_PACKAGE "" CACHE FILEPATH "Compiled Fluent package for headless/sanitizer template tests")
if(VELORA_ENABLE_ANTLR)
    add_dependencies(velora_fluent_controls_test velora_fluent_package)
    add_dependencies(velora_fluent_buttons_test velora_fluent_package)
    add_test(NAME velora_fluent_buttons_test COMMAND velora_fluent_buttons_test "${VELORA_FLUENT_PACKAGE}")
    add_test(NAME velora_fluent_controls_test COMMAND velora_fluent_controls_test "${VELORA_FLUENT_PACKAGE}")
elseif(VELORA_FLUENT_TEST_PACKAGE)
    add_test(NAME velora_fluent_controls_test COMMAND velora_fluent_controls_test "${VELORA_FLUENT_TEST_PACKAGE}")
    add_test(NAME velora_fluent_buttons_test COMMAND velora_fluent_buttons_test "${VELORA_FLUENT_TEST_PACKAGE}")
else()
    add_test(NAME velora_fluent_controls_test COMMAND velora_fluent_controls_test)
    add_test(NAME velora_fluent_buttons_test COMMAND velora_fluent_buttons_test)
endif()
velora_add_core_test(velora_custom_control_test tests/custom_control_test.cpp velora::controls)
target_sources(velora_custom_control_test PRIVATE examples/custom_control/ValuePad.cpp)
include("${PROJECT_SOURCE_DIR}/examples/custom_control/Types.cmake")
target_include_directories(velora_custom_control_test PRIVATE "${PROJECT_BINARY_DIR}/generated/examples")
velora_add_core_test(velora_source_control_test tests/source_control_test.cpp velora::controls)
target_sources(velora_source_control_test PRIVATE examples/custom_control/ValuePad.cpp)
target_include_directories(velora_source_control_test PRIVATE "${PROJECT_BINARY_DIR}/generated/examples")
if(VELORA_ENABLE_ANTLR)
    target_compile_definitions(velora_source_control_test PRIVATE VELORA_TEST_SOURCE_XAML=1)
    target_link_libraries(velora_source_control_test PRIVATE velora::markup)
    velora_xaml_load(velora_source_control_test XAML SourcePage.xaml HEADERS SourcePage.hpp
        TYPES ValuePad.vtypes BASE_DIR "${PROJECT_SOURCE_DIR}/examples/custom_control")
endif()

if(WIN32)
    velora_add_core_test(velora_platform_window_test tests/platform_window_test.cpp velora::platform ole32 uuid)
    target_compile_definitions(velora_platform_window_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_platform_window_test PROPERTIES RUN_SERIAL TRUE)  # real window and timing
    # R01.2: the platform HAL Dispatcher contract (thread affinity, cross-thread wake) and SurfaceDescriptor.
    velora_add_core_test(velora_platform_hal_test tests/platform_hal_test.cpp velora::platform)
    target_compile_definitions(velora_platform_hal_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_platform_hal_test PROPERTIES RUN_SERIAL TRUE)  # real queue and timing
    velora_add_core_test(velora_application_test tests/application_test.cpp
        velora::application velora::controls velora::graphics)
    set_tests_properties(velora_application_test PROPERTIES RUN_SERIAL TRUE)  # real windows and CPU timing
    # N02: Direct2D/DirectWrite drawing on a real window, checked on the read-back frame.
    velora_add_core_test(velora_graphics_test tests/graphics_test.cpp
        velora::graphics  velora::platform velora::controls)
    target_compile_definitions(velora_graphics_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_graphics_test PROPERTIES RUN_SERIAL TRUE)  # real window and CPU timing
    add_test(NAME velora_graphics_test_screenshots
        COMMAND velora_graphics_test "${CMAKE_CURRENT_BINARY_DIR}/n02-screenshots")
    set_tests_properties(velora_graphics_test_screenshots PROPERTIES RUN_SERIAL TRUE)
    # N03: Automatic frame scheduling through XamlHost + NativeSink.
    velora_add_core_test(velora_integration_test tests/integration_test.cpp
        velora::application velora::controls velora::graphics )
    target_compile_definitions(velora_integration_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_integration_test PROPERTIES RUN_SERIAL TRUE)
    # N04: XamlApplication/XamlWindow startup pattern.
    velora_add_core_test(velora_xaml_application_test tests/xaml_application_test.cpp
        velora::application velora::controls velora::graphics )
    target_compile_definitions(velora_xaml_application_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_xaml_application_test PROPERTIES RUN_SERIAL TRUE)
    # R04: two windows, two isolated XamlRoots (size, DPI, theme, focus, Content lifecycle, close).
    velora_add_core_test(velora_xaml_root_test tests/xaml_root_test.cpp
        velora::application velora::controls velora::graphics )
    target_compile_definitions(velora_xaml_root_test PRIVATE UNICODE _UNICODE NOMINMAX)
    set_tests_properties(velora_xaml_root_test PROPERTIES RUN_SERIAL TRUE)
endif()

if(VELORA_ENABLE_ANTLR)
    # Build-time compilers (M02/M03/M07/M08): reflection front ends, XAML parser and velorac.
    # Use in-repo test data; external reference path removed per I00 requirement
    set(_rkgen_testdata "${PROJECT_SOURCE_DIR}/tests/rkgen/testdata")
    add_executable(velora_rkgen_frontend_test tests/rkgen_frontend_test.cpp)
    target_link_libraries(velora_rkgen_frontend_test PRIVATE velora::markup)
    add_test(NAME velora_rkgen_frontend_test
        COMMAND velora_rkgen_frontend_test "${PROJECT_SOURCE_DIR}/tests/rkgen" "${_rkgen_testdata}")
    add_executable(velora_metadata_test tests/metadata_test.cpp)
    target_link_libraries(velora_metadata_test PRIVATE velora::markup)
    velora_reflect_generate(velora_metadata_test
        BASE_DIR "${PROJECT_SOURCE_DIR}/tests/rkmeta/fixtures"
        INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/tests/rkmeta/fixtures"
        HEADERS tests/rkmeta/fixtures/controls.hpp)
    target_include_directories(velora_metadata_test PRIVATE "${PROJECT_SOURCE_DIR}/tests/rkmeta/fixtures")
    add_test(NAME velora_metadata_test
        COMMAND velora_metadata_test "${PROJECT_SOURCE_DIR}/tests/rkmeta" "${_rkgen_testdata}" "${PROJECT_SOURCE_DIR}/tests/rkmeta/fixtures")
    add_executable(velora_xaml_test tests/xaml_test.cpp)
    target_link_libraries(velora_xaml_test PRIVATE velora::markup)
    add_test(NAME velora_xaml_test COMMAND velora_xaml_test "${PROJECT_SOURCE_DIR}/tests/xaml")
    add_executable(velorac_test tests/velorac_test.cpp)
    target_link_libraries(velorac_test PRIVATE velora::markup)
    add_test(NAME velorac_test COMMAND velorac_test "${PROJECT_SOURCE_DIR}/tests/velorac")
    # Incremental generation through the installed CMake helpers (generation-only sub-builds).
    find_program(VELORA_TEST_NINJA NAMES ninja REQUIRED)
    foreach(_suite IN ITEMS rkmeta velorac)
        set(_name velora_${_suite}_incremental)
        if(_suite STREQUAL "velorac")
            set(_name velorac_incremental)
        endif()
        add_test(NAME ${_name}
            COMMAND "${CMAKE_COMMAND}"
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/${_suite}/incremental
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${_suite}-incremental
                -DVELORAC=$<TARGET_FILE:velorac>
                -DVELORA_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake
                -DMAKE_PROGRAM=${VELORA_TEST_NINJA}
                -DROOT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${PROJECT_SOURCE_DIR}/tests/${_suite}/incremental/run.cmake)
        set_tests_properties(${_name} PROPERTIES RUN_SERIAL TRUE)
    endforeach()

    # R12.1: M09 item x:Bind, events and recycling through the binary loader.
    add_executable(velora_m09_vxbf_test tests/m09_vxbf_test.cpp)
    velora_xaml_load(velora_m09_vxbf_test
        BASE_DIR "${PROJECT_SOURCE_DIR}/examples/m09_bindings"
        HEADERS Model.hpp
        REFLECT Model.hpp
        XAML BindingPage.xaml)
    add_test(NAME velora_m09_vxbf_test COMMAND velora_m09_vxbf_test)
    set(_m10 "${PROJECT_SOURCE_DIR}/examples/m10_styles")
    # R12.1: M10 acceptance through the .vxbf pipeline.
    add_executable(velora_m10_vxbf_test tests/m10_vxbf_test.cpp)
    velora_xaml_load(velora_m10_vxbf_test
        BASE_DIR "${_m10}"
        HEADERS Styles.hpp
        XAML StylesPage.xaml SystemResources.xaml WinUICornerRadius.xaml WinUIButton.xaml WinUITextBox.xaml)
    add_test(NAME velora_m10_vxbf_test COMMAND velora_m10_vxbf_test)
    # R06/R07: the .vxbf pipeline — format checks, same-named pages in two directories, loading.
    add_executable(velora_vxbf_test tests/vxbf_test.cpp)
    velora_xaml_load(velora_vxbf_test
        BASE_DIR "${PROJECT_SOURCE_DIR}/tests/vxbf"
        HEADERS Controllers.hpp XBindModel.hpp
        REFLECT XBindModel.hpp
        XAML Views/A/Main.xaml Views/B/Main.xaml Styled.xaml Broken.xaml Themed.xaml Cycle.xaml
            Theme/Shared.xaml Bound.xaml XBound.xaml Localized.xaml States.xaml Listed.xaml)
    target_link_libraries(velora_vxbf_test PRIVATE velora::markup) # builder diagnostics
    add_test(NAME velora_vxbf_test COMMAND velora_vxbf_test)
    # R08: open an external resource package and compare it with the same loose resource tree.
    add_executable(velora_resource_pack_build_test tests/resource_pack_build_test.cpp)
    target_link_libraries(velora_resource_pack_build_test PRIVATE velora::application velora::resources)
    velora_add_resources(velora_resource_pack_build_test
        ROOT "${PROJECT_SOURCE_DIR}/tests/resources/app"
        NAME App
        COMPRESS EXTERNAL)
    set(_resource_test_package
        "$<TARGET_FILE_DIR:velora_resource_pack_build_test>/Resources/App.vpak")
    add_test(NAME velora_resource_pack_build_test
        COMMAND velora_resource_pack_build_test "${_resource_test_package}"
            "${PROJECT_SOURCE_DIR}/tests/resources/app")
    # R11.1: the Fluent framework package, merged by pages and overridden by the application.
    add_executable(velora_fluent_test tests/fluent_test.cpp)
    target_link_libraries(velora_fluent_test PRIVATE velora::fluent)
    velora_xaml_load(velora_fluent_test
        BASE_DIR "${PROJECT_SOURCE_DIR}/tests/fluent"
        HEADERS Pages.hpp
        XAML Plain.xaml Overridden.xaml FirstBatch.xaml SecondBatch.xaml)
    add_dependencies(velora_fluent_test velora_fluent_package)
    add_test(NAME velora_fluent_test COMMAND velora_fluent_test "${VELORA_FLUENT_PACKAGE}")
    add_test(NAME velora_resource_verify_pack
        COMMAND velorac verify-pack "${_resource_test_package}")
    set_tests_properties(velora_resource_verify_pack PROPERTIES DEPENDS velora_resource_pack_build_test)
    if(WIN32)
        # N07 Gallery acceptance on a real window (Direct2D/DirectWrite); screenshots in the
        # build directory.
        # R12.1: Gallery migrated to .vxbf pipeline
        add_executable(velora_gallery_test tests/gallery_test.cpp examples/gallery/Gallery.cpp)
        velora_xaml_load(velora_gallery_test
            BASE_DIR "${PROJECT_SOURCE_DIR}/examples/gallery"
            HEADERS Gallery.hpp
            REFLECT Gallery.hpp
            XAML GalleryPage.xaml)
        target_link_libraries(velora_gallery_test PRIVATE velora::graphics  velora::platform)
        target_compile_definitions(velora_gallery_test PRIVATE UNICODE _UNICODE NOMINMAX)
        add_test(NAME velora_gallery_test
            COMMAND velora_gallery_test "${CMAKE_CURRENT_BINARY_DIR}/gallery-screenshots")
        set_tests_properties(velora_gallery_test PROPERTIES RUN_SERIAL TRUE)
    endif()

    # The installed package works for a standalone find_package(Velora) application.
    # The consumer links the rhodium artifacts of the configuration under test.
    get_filename_component(_rhodium_debug "${RHODIUM_DEPS_FILE}" DIRECTORY)
    get_filename_component(_rhodium_release "${RHODIUM_RELEASE_DEPS_FILE}" DIRECTORY)
    set(_rhodium_dir "$<IF:$<CONFIG:Debug>,${_rhodium_debug},${_rhodium_release}>")
    add_test(NAME velora_install_consumer
        COMMAND "${CMAKE_COMMAND}"
            -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-consumer -DCONFIG=$<CONFIG>
            -DRHODIUM_DIR=${_rhodium_dir} -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P ${PROJECT_SOURCE_DIR}/tests/install_consumer.cmake)
    set_tests_properties(velora_install_consumer PROPERTIES RUN_SERIAL TRUE)
endif()

# Every public header compiles on its own (N00): one translation unit per header.
file(GLOB_RECURSE _public_headers RELATIVE "${PROJECT_SOURCE_DIR}/modules"
    "${PROJECT_SOURCE_DIR}/modules/*/src/velora/*.hpp")
list(FILTER _public_headers EXCLUDE REGEX "Internal\.hpp$|CompilerImpl\.hpp$")
if(NOT WIN32)
    list(FILTER _public_headers EXCLUDE REGEX "^platform/|/windows/")
endif()
if(NOT VELORA_ENABLE_ANTLR)
    list(FILTER _public_headers EXCLUDE REGEX "^markup/")
endif()
set(_header_units)
foreach(_header IN LISTS _public_headers)
    string(REGEX REPLACE "^[a-z]+/src/" "" _include "${_header}")
    string(MAKE_C_IDENTIFIER "${_include}" _unit)
    set(_file "${CMAKE_CURRENT_BINARY_DIR}/header-check/${_unit}.cpp")
    set(_text "#include <${_include}>
")
    if(EXISTS "${_file}")
        file(READ "${_file}" _old)
    endif()
    if(NOT _old STREQUAL _text)
        file(WRITE "${_file}" "${_text}")
    endif()
    list(APPEND _header_units "${_file}")
endforeach()
add_library(velora_header_check OBJECT ${_header_units})
get_property(_modules GLOBAL PROPERTY VELORA_MODULE_TARGETS)
target_link_libraries(velora_header_check PRIVATE ${_modules})
if(WIN32)
    # Backend headers (windows/, *_win32.hpp) are compiled only by their modules, which define these.
    target_compile_definitions(velora_header_check PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

# A few legacy test files used assert() with side effects; keep assertions enabled
# on test executables in Release without affecting runtime code.
get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _targets)
    get_target_property(_type ${_target} TYPE)
    if(_type STREQUAL "EXECUTABLE" AND NOT _target STREQUAL "velorac")
        if(MSVC)
            target_compile_options(${_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${_target} PRIVATE -UNDEBUG)
        endif()
    endif()
endforeach()

# CTest must locate the MSVC ASan runtime even outside a Developer Command Prompt.
if(VELORA_ENABLE_SANITIZERS AND MSVC)
    get_property(_asan_tests DIRECTORY PROPERTY TESTS)
    foreach(_test IN LISTS _asan_tests)
        set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${VELORA_ASAN_RUNTIME_DIRECTORY}")
        set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=alloc_dealloc_mismatch=1")
    endforeach()
endif()
