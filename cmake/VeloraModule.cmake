include_guard(GLOBAL)

# velora_module(<name> [SOURCES <file>...] [GENERATED <dir> <dependency target>]
#               [PUBLIC <dep>...] [PRIVATE <dep>...])
#
# Declares the library of the module in the current directory as target velora_<name>,
# alias and export name velora::<name>. Headers and sources live together under the
# module's src/ directory (src/velora/<area>/...), which is the module's include root and is
# installed as such. A module without sources is an interface (header-only) library.
# Relative SOURCES are under src/; absolute ones are build outputs. GENERATED names a build
# directory with generated code (a second include root, installed like src/) and the target
# that produces it.
function(velora_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;GENERATED;PUBLIC;PRIVATE" ${ARGN})
    set(target velora_${name})
    set(root "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(ARG_SOURCES)
        set(sources)
        foreach(source IN LISTS ARG_SOURCES)
            if(IS_ABSOLUTE "${source}")
                list(APPEND sources "${source}")
            else()
                list(APPEND sources "${root}/${source}")
            endif()
        endforeach()
        add_library(${target} STATIC ${sources})
        set(scope PUBLIC)
    else()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif()
    add_library(velora::${name} ALIAS ${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${name})
    target_compile_features(${target} ${scope} cxx_std_23)
    target_include_directories(${target} ${scope}
        $<BUILD_INTERFACE:${root}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    if(ARG_PUBLIC)
        target_link_libraries(${target} ${scope} ${ARG_PUBLIC})
    endif()
    if(ARG_PRIVATE)
        target_link_libraries(${target} PRIVATE ${ARG_PRIVATE})
    endif()
    if(WIN32 AND ARG_SOURCES)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX)
    endif()
    install(DIRECTORY "${root}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.hpp")
    if(ARG_GENERATED)
        list(GET ARG_GENERATED 0 generated)
        list(GET ARG_GENERATED 1 producer)
        target_include_directories(${target} ${scope} $<BUILD_INTERFACE:${generated}>)
        add_dependencies(${target} ${producer})
        install(DIRECTORY "${generated}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            FILES_MATCHING PATTERN "*.hpp")
    endif()
    set_property(GLOBAL APPEND PROPERTY VELORA_MODULE_TARGETS ${target})
endfunction()
