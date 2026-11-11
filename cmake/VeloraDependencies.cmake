include_guard(GLOBAL)

# rhodium is the only third-party dependency manager. Core/runtime builds are
# deliberately independent of rhodium until they need third-party components.
#
# rhodium generates one configuration per output directory:
#   rhodium generate --toolchain windows-msvc-x64 --config debug   (-> .rhodium/generated/cmake)
#   rhodium generate --toolchain windows-msvc-x64 --config release --output .rhodium/generated/cmake-release
# The debug exports define the targets; when the release exports exist, each target links the
# release library in non-Debug configurations, so one multi-config tree builds both (MSVC
# cannot mix the debug and release runtime libraries).
if(VELORA_ENABLE_ANTLR)
    set(RHODIUM_DEPS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.rhodium/generated/cmake/rhodiumConfig.cmake"
        CACHE FILEPATH "rhodium generated CMake exports (debug)")
    set(RHODIUM_RELEASE_DEPS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.rhodium/generated/cmake-release/rhodiumConfig.cmake"
        CACHE FILEPATH "rhodium generated CMake exports (release)")
    if(NOT EXISTS "${RHODIUM_DEPS_FILE}")
        message(FATAL_ERROR "Missing rhodium exports: run rhodium restore and rhodium generate; or configure with VELORA_ENABLE_ANTLR=OFF")
    endif()
    include("${RHODIUM_DEPS_FILE}")
    get_filename_component(_release_dir "${RHODIUM_RELEASE_DEPS_FILE}" DIRECTORY)
    if(EXISTS "${_release_dir}/rhodiumTargets.cmake")
        # Release library of each target: the quoted path after its INTERFACE_LINK_LIBRARIES.
        file(READ "${_release_dir}/rhodiumTargets.cmake" _release)
        string(REGEX MATCHALL "TARGET (rhodium::[A-Za-z0-9_.-]+) PROPERTY INTERFACE_LINK_LIBRARIES[ \t\r\n]+\"([^\"]+)\""
            _links "${_release}")
        foreach(_link IN LISTS _links)
            string(REGEX MATCH "TARGET (rhodium::[A-Za-z0-9_.-]+) PROPERTY INTERFACE_LINK_LIBRARIES[ \t\r\n]+\"([^\"]+)\""
                _ "${_link}")
            get_target_property(_debug ${CMAKE_MATCH_1} INTERFACE_LINK_LIBRARIES)
            set_property(TARGET ${CMAKE_MATCH_1} PROPERTY INTERFACE_LINK_LIBRARIES
                "$<IF:$<CONFIG:Debug>,${_debug},${CMAKE_MATCH_2}>")
        endforeach()
    elseif(CMAKE_CONFIGURATION_TYPES)
        message(WARNING "No release rhodium exports (${RHODIUM_RELEASE_DEPS_FILE}): only the Debug configuration "
                        "links the ANTLR-based compilers")
    endif()
endif()
