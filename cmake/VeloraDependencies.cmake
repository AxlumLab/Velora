include_guard(GLOBAL)

# tpkg is the only third-party dependency manager. Core/runtime builds are
# deliberately independent of tpkg until they need third-party components.
#
# tpkg generates one configuration per output directory:
#   tpkg generate --toolchain windows-msvc-x64 --config debug   (-> .tpkg/generated/cmake)
#   tpkg generate --toolchain windows-msvc-x64 --config release --output .tpkg/generated/cmake-release
# The debug exports define the targets; when the release exports exist, each target links the
# release library in non-Debug configurations, so one multi-config tree builds both (MSVC
# cannot mix the debug and release runtime libraries).
if(VELORA_ENABLE_ANTLR)
    set(TPKG_DEPS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.tpkg/generated/cmake/tpkgConfig.cmake"
        CACHE FILEPATH "tpkg generated CMake exports (debug)")
    set(TPKG_RELEASE_DEPS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.tpkg/generated/cmake-release/tpkgConfig.cmake"
        CACHE FILEPATH "tpkg generated CMake exports (release)")
    if(NOT EXISTS "${TPKG_DEPS_FILE}")
        message(FATAL_ERROR "Missing tpkg exports: run tpkg restore and tpkg generate; or configure with VELORA_ENABLE_ANTLR=OFF")
    endif()
    include("${TPKG_DEPS_FILE}")
    get_filename_component(_release_dir "${TPKG_RELEASE_DEPS_FILE}" DIRECTORY)
    if(EXISTS "${_release_dir}/tpkgTargets.cmake")
        # Release library of each target: the quoted path after its INTERFACE_LINK_LIBRARIES.
        file(READ "${_release_dir}/tpkgTargets.cmake" _release)
        string(REGEX MATCHALL "TARGET (tpkg::[A-Za-z0-9_.-]+) PROPERTY INTERFACE_LINK_LIBRARIES[ \t\r\n]+\"([^\"]+)\""
            _links "${_release}")
        foreach(_link IN LISTS _links)
            string(REGEX MATCH "TARGET (tpkg::[A-Za-z0-9_.-]+) PROPERTY INTERFACE_LINK_LIBRARIES[ \t\r\n]+\"([^\"]+)\""
                _ "${_link}")
            get_target_property(_debug ${CMAKE_MATCH_1} INTERFACE_LINK_LIBRARIES)
            set_property(TARGET ${CMAKE_MATCH_1} PROPERTY INTERFACE_LINK_LIBRARIES
                "$<IF:$<CONFIG:Debug>,${_debug},${CMAKE_MATCH_2}>")
        endforeach()
    elseif(CMAKE_CONFIGURATION_TYPES)
        message(WARNING "No release tpkg exports (${TPKG_RELEASE_DEPS_FILE}): only the Debug configuration "
                        "links the ANTLR-based compilers")
    endif()
endif()
