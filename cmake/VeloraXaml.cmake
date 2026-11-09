# DEPRECATED: velora_xaml_compile is the legacy DSL compiler that generates m_ui.set() calls.
# Use velora_xaml_load() for new code — it compiles to .vxbf and loads through XamlLoader.
# velora_xaml_compile will be removed in a future release.
#
# velora_xaml_compile(<target> XAML <file>... HEADERS <controller header>... BASE_DIR <dir>
#                     [RESOURCES <ResourceDictionary file>...])
# The tool is the velorac target when present, else VELORAC_EXECUTABLE.
#
# Compiles XAML views into <target>:
#   1. velora_reflect_generate() produces .rkmeta (and static accessor checks) for HEADERS,
#      the C++ controllers and view models the XAML refers to;
#   2. `velorac xaml` turns each XAML file into <stem>.g.hpp/.g.cpp under
#      <binary dir>/xaml/<target>, which is added to the target's include path.
# RESOURCES are ResourceDictionary files that views merge (MergedDictionaries Source="..."):
# each is checked once by `velorac resources` and passed to every view compilation.
# Each XAML output depends on its XAML file, the tool and all metadata of the target; the
# depfile adds the peer XAML files it actually uses as custom controls. velorac rewrites
# outputs only when their content changes (Ninja restats them), so editing one XAML file
# recompiles only that view, and a metadata change recompiles only views whose code changed.
function(velora_xaml_compile target)
    cmake_parse_arguments(ARG "" "BASE_DIR" "XAML;HEADERS;RESOURCES" ${ARGN})
    if(TARGET velorac)
        set(tool "$<TARGET_FILE:velorac>")
        set(tool_dependency velorac)
    elseif(VELORAC_EXECUTABLE)
        set(tool "${VELORAC_EXECUTABLE}")
        set(tool_dependency "${VELORAC_EXECUTABLE}")
    else()
        message(FATAL_ERROR "velora_xaml_compile needs the velorac target (VELORA_ENABLE_ANTLR=ON) or VELORAC_EXECUTABLE")
    endif()
    get_filename_component(base "${ARG_BASE_DIR}" ABSOLUTE)
    velora_reflect_generate(${target} BASE_DIR "${base}" INCLUDE_ROOT "${base}" HEADERS ${ARG_HEADERS})
    set(meta_args "")
    set(meta_files "")
    foreach(header IN LISTS ARG_HEADERS)
        get_filename_component(header "${header}" ABSOLUTE)
        get_filename_component(stem "${header}" NAME_WE)
        file(RELATIVE_PATH include "${base}" "${header}")
        set(meta "${CMAKE_CURRENT_BINARY_DIR}/rkmeta/${target}/${stem}.rkmeta")
        list(APPEND meta_args --meta "${meta}=${include}")
        list(APPEND meta_files "${meta}")
    endforeach()
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/xaml/${target}")
    set(dictionaries "")
    foreach(dictionary IN LISTS ARG_RESOURCES)
        get_filename_component(dictionary "${dictionary}" ABSOLUTE)
        list(APPEND dictionaries "${dictionary}")
    endforeach()
    set(dictionary_args "")
    set(dictionary_stamps "")
    foreach(dictionary IN LISTS dictionaries)
        list(APPEND dictionary_args --peer "${dictionary}")
    endforeach()
    foreach(dictionary IN LISTS dictionaries)
        get_filename_component(stem "${dictionary}" NAME_WE)
        set(stamp "${out_dir}/${stem}.resources.ok")
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
            COMMAND "${tool}" resources ${dictionary_args} "${dictionary}" "${stamp}"
            DEPENDS "${dictionary}" ${tool_dependency} ${dictionaries}
            DEPFILE "${stamp}.d"
            COMMENT "velorac ${stem}.xaml (resources)"
            VERBATIM)
        list(APPEND dictionary_stamps "${stamp}")
    endforeach()
    set(sources "")
    set(compiled_xaml "")
    set(compiled_relpaths "")
    foreach(xaml IN LISTS ARG_XAML)
        get_filename_component(xaml "${xaml}" ABSOLUTE)
        list(APPEND sources "${xaml}")
    endforeach()
    foreach(xaml IN LISTS sources)
        # Outputs mirror the file's path under BASE_DIR (R06.5): Views/A/Main.xaml and
        # Views/B/Main.xaml produce Views/A/Main.g.cpp and Views/B/Main.g.cpp, not one Main.g.cpp.
        get_filename_component(stem "${xaml}" NAME_WE)
        file(RELATIVE_PATH relative "${base}" "${xaml}")
        get_filename_component(relative_dir "${relative}" DIRECTORY)
        set(view_dir "${out_dir}")
        if(relative_dir)
            set(view_dir "${out_dir}/${relative_dir}")
        endif()
        set(peer_args "")
        foreach(other IN LISTS sources)
            if(NOT other STREQUAL xaml)
                list(APPEND peer_args --peer "${other}")
            endif()
        endforeach()
        add_custom_command(
            OUTPUT "${view_dir}/${stem}.g.hpp" "${view_dir}/${stem}.g.cpp"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${view_dir}"
            COMMAND "${tool}" xaml ${meta_args} ${peer_args} ${dictionary_args} "${xaml}" "${view_dir}"
            DEPENDS "${xaml}" ${tool_dependency} ${meta_files} ${dictionary_stamps}
            DEPFILE "${view_dir}/${stem}.g.d"
            COMMENT "velorac ${relative}"
            VERBATIM)
        target_sources(${target} PRIVATE "${view_dir}/${stem}.g.cpp")
    endforeach()
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "UTILITY")
        target_include_directories(${target} PRIVATE "${out_dir}" "${base}")
        target_link_libraries(${target} PRIVATE velora::controls)
    endif()
endfunction()

# velora_xaml_load(<target> XAML <file>... BASE_DIR <dir> [HEADERS <controller header>...]
#                  [REFLECT <header>...])
#
# The .vxbf pipeline (R06/R07): each XAML page (x:Class root) compiles to <rel>.vxbf plus a
# code-behind <rel>.g.hpp/.g.cpp under <binary dir>/vxbf/<target>, where <rel> is the file's path
# relative to BASE_DIR. The code-behind embeds the .vxbf and loads it at run time with
# velora::loadComponent; it contains no tree construction. HEADERS are the controller headers the
# code-behind includes (relative to BASE_DIR, as the target includes them). REFLECT headers (with
# rkclass annotations) get reflection metadata, against which x:Bind paths are checked and compiled.
function(velora_xaml_load target)
    cmake_parse_arguments(ARG "NO_EMBED" "BASE_DIR" "XAML;HEADERS;REFLECT" ${ARGN})
    if(TARGET velorac)
        set(tool "$<TARGET_FILE:velorac>")
        set(tool_dependency velorac)
    elseif(VELORAC_EXECUTABLE)
        set(tool "${VELORAC_EXECUTABLE}")
        set(tool_dependency "${VELORAC_EXECUTABLE}")
    else()
        message(FATAL_ERROR "velora_xaml_load needs the velorac target (VELORA_ENABLE_ANTLR=ON) or VELORAC_EXECUTABLE")
    endif()
    get_filename_component(base "${ARG_BASE_DIR}" ABSOLUTE)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/vxbf/${target}")
    set(include_args "")
    set(header_files "")
    foreach(header IN LISTS ARG_HEADERS)
        get_filename_component(absolute "${header}" ABSOLUTE BASE_DIR "${base}")
        file(RELATIVE_PATH include "${base}" "${absolute}")
        list(APPEND include_args --include "${include}")
        list(APPEND header_files "${absolute}")
    endforeach()
    set(meta_args "")
    set(meta_files "")
    if(ARG_REFLECT)
        set(reflect_headers "")
        foreach(header IN LISTS ARG_REFLECT)
            get_filename_component(absolute "${header}" ABSOLUTE BASE_DIR "${base}")
            get_filename_component(stem "${absolute}" NAME_WE)
            list(APPEND reflect_headers "${absolute}")
            set(meta "${CMAKE_CURRENT_BINARY_DIR}/rkmeta/${target}/${stem}.rkmeta")
            list(APPEND meta_args --meta "${meta}")
            list(APPEND meta_files "${meta}")
        endforeach()
        velora_reflect_generate(${target} BASE_DIR "${base}" INCLUDE_ROOT "${base}" HEADERS ${reflect_headers})
    endif()
    foreach(xaml IN LISTS ARG_XAML)
        get_filename_component(xaml "${xaml}" ABSOLUTE BASE_DIR "${base}")
        file(RELATIVE_PATH relative "${base}" "${xaml}")
        string(REGEX REPLACE "\\.xaml$" "" relative_stem "${relative}")
        set(outputs "${out_dir}/${relative_stem}.vxbf" "${out_dir}/${relative_stem}.g.hpp"
            "${out_dir}/${relative_stem}.g.cpp")
        set(embed_arg "")
        if(ARG_NO_EMBED)
            set(embed_arg --no-embed)
        endif()
        add_custom_command(
            OUTPUT ${outputs}
            COMMAND "${tool}" vxbf ${embed_arg} ${include_args} ${meta_args} "${xaml}" "${base}" "${out_dir}"
            DEPENDS "${xaml}" ${tool_dependency} ${header_files} ${meta_files}
            COMMENT "velorac vxbf ${relative}"
            VERBATIM)
        target_sources(${target} PRIVATE "${out_dir}/${relative_stem}.g.cpp")
        list(APPEND compiled_xaml "${out_dir}/${relative_stem}.vxbf")
        list(APPEND compiled_relpaths "${relative_stem}.vxbf")
    endforeach()
    set_property(TARGET ${target} PROPERTY VELORA_XAML_BINARIES "${compiled_xaml}")
    set_property(TARGET ${target} PROPERTY VELORA_XAML_PATHS "${compiled_relpaths}")
    target_include_directories(${target} PRIVATE "${out_dir}" "${base}")
    target_link_libraries(${target} PRIVATE velora::controls)
endfunction()

# velora_add_resources(<target> ROOT <dir> NAME <package> [LANGUAGE <tag>] [VERSION <v>] [COMPRESS]
#                      [INDEX_ONLY] [EXTERNAL])
#
# `velorac pack` turns the tree under ROOT (files named by qualifier,
# Strings/<language>/*.resw) into <NAME>.vpak, embedded by default. EXTERNAL deploys it under
# Resources/ beside <target>'s binary. INDEX_ONLY writes <NAME>.vpri instead, for loose
# resources served from ROOT by a LooseFileProvider. The output depends on every file under ROOT, so
# editing a string or adding a scale variant rebuilds the package.
function(velora_add_resources target)
    cmake_parse_arguments(ARG "COMPRESS;INDEX_ONLY;EXTERNAL" "ROOT;NAME;LANGUAGE;VERSION" "" ${ARGN})
    if(TARGET velorac)
        set(tool "$<TARGET_FILE:velorac>")
        set(tool_dependency velorac)
    elseif(VELORAC_EXECUTABLE)
        set(tool "${VELORAC_EXECUTABLE}")
        set(tool_dependency "${VELORAC_EXECUTABLE}")
    else()
        message(FATAL_ERROR "velora_add_resources needs the velorac target (VELORA_ENABLE_ANTLR=ON) or VELORAC_EXECUTABLE")
    endif()
    if(NOT ARG_ROOT OR NOT ARG_NAME)
        message(FATAL_ERROR "velora_add_resources needs ROOT and NAME")
    endif()
    get_filename_component(root "${ARG_ROOT}" ABSOLUTE)
    if(NOT ARG_LANGUAGE)
        set(ARG_LANGUAGE en-US)
    endif()
    if(NOT ARG_VERSION)
        set(ARG_VERSION 1.0.0)
    endif()
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${root}/*")
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/resources/${target}")
    if(ARG_INDEX_ONLY)
        set(output "${out_dir}/${ARG_NAME}.vpri")
        set(command "${tool}" pri --name "${ARG_NAME}" --language "${ARG_LANGUAGE}" "${root}" "${output}")
    else()
        set(output "${out_dir}/${ARG_NAME}.vpak")
        set(compress "")
        if(ARG_COMPRESS)
            set(compress --compress)
        endif()
        set(command "${tool}" pack --name "${ARG_NAME}" --version "${ARG_VERSION}" --language "${ARG_LANGUAGE}"
            ${compress} "${root}" "${output}")
    endif()
    get_filename_component(file_name "${output}" NAME)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${command}
        DEPENDS ${inputs} ${tool_dependency}
        COMMENT "velorac ${file_name}"
        VERBATIM)
    add_custom_target(${target}_resources DEPENDS "${output}")
    add_dependencies(${target} ${target}_resources)
    if(ARG_INDEX_ONLY)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${output}"
                "$<TARGET_FILE_DIR:${target}>/Resources/${file_name}" VERBATIM)
        return()
    endif()
    if(ARG_EXTERNAL OR NOT VELORA_EMBED_RESOURCES)
        set(source "${out_dir}/${target}.ExternalResources.cpp")
        add_custom_command(OUTPUT "${source}"
            COMMAND "${tool}" external-pack "${file_name}" "${source}" app
            DEPENDS ${tool_dependency} VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${output}"
                "$<TARGET_FILE_DIR:${target}>/Resources/${file_name}" VERBATIM)
    else()
        set(source "${out_dir}/${target}.EmbeddedResources.cpp")
        add_custom_command(OUTPUT "${source}"
            COMMAND "${tool}" embed-pack "${output}" "${source}" app
            DEPENDS "${output}" ${tool_dependency} VERBATIM)
    endif()
    target_sources(${target} PRIVATE "${source}")
    target_link_libraries(${target} PRIVATE velora::resources)
endfunction()

# velora_add_xaml(<target> BASE_DIR <dir> XAML ... [HEADERS ...] [REFLECT ...]
#                 [RESOURCES <dir>] [PACKAGE <name>] [EXTERNAL] [COMPRESS])
# One .vpak contains every page .vxbf, .vpri and optional asset tree. The generated code-behind
# contains no XAML bytes; it loads by URI. Default deployment embeds the *package* into the
# executable, analogous to Qt's qrc. EXTERNAL puts Resources/<name>.vpak beside the executable.
function(velora_add_xaml target)
    cmake_parse_arguments(ARG "COMPRESS;EXTERNAL" "BASE_DIR;RESOURCES;PACKAGE;LANGUAGE;VERSION" "XAML;HEADERS;REFLECT" ${ARGN})
    if(NOT ARG_BASE_DIR OR NOT ARG_XAML)
        message(FATAL_ERROR "velora_add_xaml needs BASE_DIR and XAML")
    endif()
    if(NOT ARG_PACKAGE)
        set(ARG_PACKAGE "${target}")
    endif()
    if(NOT ARG_VERSION)
        set(ARG_VERSION "1.0.0")
    endif()
    if(NOT ARG_LANGUAGE)
        set(ARG_LANGUAGE "en-US")
    endif()
    if(TARGET velorac)
        set(tool "$<TARGET_FILE:velorac>")
        set(tool_dependency velorac)
    elseif(VELORAC_EXECUTABLE)
        set(tool "${VELORAC_EXECUTABLE}")
        set(tool_dependency "${VELORAC_EXECUTABLE}")
    else()
        message(FATAL_ERROR "velora_add_xaml needs velorac or VELORAC_EXECUTABLE")
    endif()
    velora_xaml_load(${target} NO_EMBED BASE_DIR "${ARG_BASE_DIR}"
        XAML ${ARG_XAML} HEADERS ${ARG_HEADERS} REFLECT ${ARG_REFLECT})
    get_target_property(compiled ${target} VELORA_XAML_BINARIES)
    get_target_property(paths ${target} VELORA_XAML_PATHS)
    set(stage "${CMAKE_CURRENT_BINARY_DIR}/vpak/${target}/stage")
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/vpak/${target}")
    set(copy_steps COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}")
    set(inputs ${compiled})
    if(ARG_RESOURCES)
        get_filename_component(assets "${ARG_RESOURCES}" ABSOLUTE)
        file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${assets}/*")
        list(APPEND inputs ${asset_files})
        list(APPEND copy_steps COMMAND "${CMAKE_COMMAND}" -E copy_directory "${assets}" "${stage}")
    endif()
    list(LENGTH compiled count)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        list(GET compiled ${i} source)
        list(GET paths ${i} relative)
        get_filename_component(directory "${relative}" DIRECTORY)
        list(APPEND copy_steps COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}/${directory}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${stage}/${relative}")
    endforeach()
    set(compress "")
    if(ARG_COMPRESS)
        set(compress --compress)
    endif()
    set(package "${out_dir}/${ARG_PACKAGE}.vpak")
    add_custom_command(OUTPUT "${package}"
        ${copy_steps}
        COMMAND "${tool}" pack --name "${ARG_PACKAGE}" --version "${ARG_VERSION}"
            --language "${ARG_LANGUAGE}" ${compress} "${stage}" "${package}"
        DEPENDS ${inputs} ${tool_dependency}
        COMMENT "Velora package ${ARG_PACKAGE}.vpak (XAML + assets)" VERBATIM)
    if(ARG_EXTERNAL OR NOT VELORA_EMBED_RESOURCES)
        set(registration "${out_dir}/${target}.ExternalPackage.cpp")
        add_custom_command(OUTPUT "${registration}"
            COMMAND "${tool}" external-pack "${ARG_PACKAGE}.vpak" "${registration}" app
            DEPENDS ${tool_dependency}
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${package}"
                "$<TARGET_FILE_DIR:${target}>/Resources/${ARG_PACKAGE}.vpak" VERBATIM)
        add_custom_target(${target}_resources DEPENDS "${package}")
        add_dependencies(${target} ${target}_resources)
    else()
        set(registration "${out_dir}/${target}.EmbeddedPackage.cpp")
        add_custom_command(OUTPUT "${registration}"
            COMMAND "${tool}" embed-pack "${package}" "${registration}" app
            DEPENDS "${package}" ${tool_dependency}
            COMMENT "Embed complete ${ARG_PACKAGE}.vpak" VERBATIM)
    endif()
    target_sources(${target} PRIVATE "${registration}")
    target_link_libraries(${target} PRIVATE velora::resources)
endfunction()
