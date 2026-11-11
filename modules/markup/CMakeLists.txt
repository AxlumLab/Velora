# Build-time compilers: the ANTLR reflection frontend, metadata generation, the XAML parser and compiler, and the
# velorac tool. Applications never link these at run time.
set(VELORA_ANTLR_TOOL_JAR "" CACHE FILEPATH "Pinned ANTLR 4.13.2 complete jar")
set(_antlr_sha256 "eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76")
# Default: the checksum-verified build tool retained outside managed dependency caches.
set(_antlr_cached
    "${PROJECT_SOURCE_DIR}/.rhodium/tools/antlr4-4.13.2-complete.jar")
if(NOT EXISTS "${VELORA_ANTLR_TOOL_JAR}" AND EXISTS "${_antlr_cached}")
    set(VELORA_ANTLR_TOOL_JAR "${_antlr_cached}" CACHE FILEPATH "Pinned ANTLR 4.13.2 complete jar" FORCE)
endif()
if(NOT EXISTS "${VELORA_ANTLR_TOOL_JAR}")
    message(FATAL_ERROR "VELORA_ENABLE_ANTLR needs -DVELORA_ANTLR_TOOL_JAR=<antlr-4.13.2-complete.jar>")
endif()
file(SHA256 "${VELORA_ANTLR_TOOL_JAR}" _sha)
if(NOT _sha STREQUAL _antlr_sha256)
    message(FATAL_ERROR "ANTLR tool jar checksum mismatch: ${_sha}")
endif()
if(NOT TARGET rhodium::antlr4-runtime)
    message(FATAL_ERROR "ANTLR C++ runtime missing from the rhodium exports")
endif()
find_package(Java 11 COMPONENTS Runtime REQUIRED)

# Generates C++ parsers from grammars into <binary dir>/antlr/<name>; sets <out_var>.
function(velora_antlr name out_var)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/antlr/${name}")
    cmake_parse_arguments(ARG "" "" "GRAMMARS;OUTPUTS" ${ARGN})
    list(TRANSFORM ARG_GRAMMARS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/grammar/${name}/")
    list(TRANSFORM ARG_OUTPUTS PREPEND "${dir}/")
    list(TRANSFORM ARG_OUTPUTS APPEND ".cpp")
    add_custom_command(OUTPUT ${ARG_OUTPUTS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}"
        COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${VELORA_ANTLR_TOOL_JAR}"
            -Dlanguage=Cpp -no-listener -no-visitor -Xexact-output-dir -o "${dir}" -lib "${dir}" ${ARG_GRAMMARS}
        DEPENDS ${ARG_GRAMMARS} "${VELORA_ANTLR_TOOL_JAR}"
        VERBATIM)
    if(MSVC)
        set_source_files_properties(${ARG_OUTPUTS} PROPERTIES COMPILE_OPTIONS "/W0")
    endif()
    set(${out_var} ${ARG_OUTPUTS} PARENT_SCOPE)
    set(${out_var}_DIR "${dir}" PARENT_SCOPE)
endfunction()

velora_antlr(reflect _reflect_parser
    GRAMMARS VeloraReflectLexer.g4 VeloraReflectParser.g4
    OUTPUTS VeloraReflectLexer VeloraReflectParser)
velora_antlr(xaml _xaml_parser
    GRAMMARS VeloraXmlLexer.g4 VeloraXmlParser.g4 VeloraMarkup.g4 VeloraBind.g4
    OUTPUTS VeloraXmlLexer VeloraXmlParser VeloraMarkupLexer VeloraMarkupParser VeloraBindLexer VeloraBindParser)

velora_module(markup
    SOURCES
        velora/reflect/Compiler.cpp
        velora/reflect/AntlrFrontend.cpp
        velora/reflect/Metagen.cpp
        velora/xaml/Source.cpp
        velora/xaml/Markup.cpp
        velora/xaml/Parser.cpp
        velora/xaml/Dump.cpp
        velora/xaml/Compiler.cpp
        velora/xaml/CompilerAttributes.cpp
        velora/xaml/CompilerControls.cpp
        velora/xaml/CompilerEmit.cpp
        velora/xaml/CompilerResources.cpp
        velora/xaml/CompilerStates.cpp
        velora/xaml/CompilerStyles.cpp
        velora/xaml/XamlIRBuilder.cpp
        velora/xaml/CodeBehindWriter.cpp
        velora/xaml/ReswReader.cpp
    PUBLIC velora::reflect velora::controls velora::resources
    PRIVATE rhodium::antlr4-runtime)
target_sources(velora_markup PRIVATE ${_reflect_parser} ${_xaml_parser})
target_include_directories(velora_markup PRIVATE "${_reflect_parser_DIR}" "${_xaml_parser_DIR}")
target_compile_definitions(velora_markup PRIVATE ANTLR4CPP_STATIC)

add_executable(velorac ${CMAKE_CURRENT_SOURCE_DIR}/src/velora/tools/Velorac.cpp)
target_link_libraries(velorac PRIVATE velora::markup)
install(TARGETS velorac RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Build helpers for applications: velora_reflect_generate() and velora_xaml_compile().
include(${PROJECT_SOURCE_DIR}/cmake/VeloraReflect.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/VeloraXaml.cmake)
