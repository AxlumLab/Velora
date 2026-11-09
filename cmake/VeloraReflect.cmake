# velora_reflect_generate(<target> HEADERS <h>... [BASE_DIR <dir>] [INCLUDE_ROOT <dir>] [DEFINES <name>...])
#
# Runs `velorac reflect` once per header at build time. Each header produces
#   <binary dir>/rkmeta/<target>/<stem>.rkmeta        (metadata, input to later XAML stages)
#   <binary dir>/rkmeta/<target>/<stem>.reflect.cpp   (compiled into <target>)
# Outputs depend on the header, the tool and, through a depfile, every include the scan read,
# so editing one header regenerates only its own outputs. BASE_DIR is the include directory
# the header is reached from (#include "<path relative to BASE_DIR>"); with it the generated
# code compiles static checks of every annotated accessor against the real header.
# The .rkmeta paths are appended to the target property VELORA_RKMETA_FILES. The tool is
# the velorac target when present, else VELORAC_EXECUTABLE.
function(velora_reflect_generate target)
    cmake_parse_arguments(ARG "" "BASE_DIR;INCLUDE_ROOT" "HEADERS;DEFINES" ${ARGN})
    if(TARGET velorac)
        set(tool "$<TARGET_FILE:velorac>")
        set(tool_dependency velorac)
    elseif(VELORAC_EXECUTABLE)
        set(tool "${VELORAC_EXECUTABLE}")
        set(tool_dependency "${VELORAC_EXECUTABLE}")
    else()
        message(FATAL_ERROR "velora_reflect_generate: no velorac target or VELORAC_EXECUTABLE")
    endif()
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/rkmeta/${target}")
    set(flags "")
    foreach(define IN LISTS ARG_DEFINES)
        list(APPEND flags "-D${define}")
    endforeach()
    if(ARG_INCLUDE_ROOT)
        list(APPEND flags "-I${ARG_INCLUDE_ROOT}")
    endif()
    set(outputs "")
    foreach(header IN LISTS ARG_HEADERS)
        get_filename_component(header "${header}" ABSOLUTE)
        get_filename_component(stem "${header}" NAME_WE)
        string(MAKE_C_IDENTIFIER "${target}_${stem}" unit)
        set(meta "${out_dir}/${stem}.rkmeta")
        set(cpp "${out_dir}/${stem}.reflect.cpp")
        set(include_flags "")
        if(ARG_BASE_DIR)
            file(RELATIVE_PATH include_path "${ARG_BASE_DIR}" "${header}")
            set(include_flags --include "${include_path}")
        endif()
        add_custom_command(
            OUTPUT "${meta}" "${cpp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
            COMMAND "${tool}" reflect ${flags} --unit "${unit}" ${include_flags} "${header}" "${meta}" "${cpp}"
            DEPENDS "${header}" ${tool_dependency}
            DEPFILE "${meta}.d"
            COMMENT "rkmeta ${stem}"
            VERBATIM)
        target_sources(${target} PRIVATE "${cpp}")
        list(APPEND outputs "${meta}")
    endforeach()
    set_property(TARGET ${target} APPEND PROPERTY VELORA_RKMETA_FILES ${outputs})
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "UTILITY")
        target_link_libraries(${target} PRIVATE velora::reflect)
    endif()
endfunction()
