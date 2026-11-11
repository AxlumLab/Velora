-- Velora's third-party dependencies, all managed by Rhodium. Only the ANTLR C++ runtime is
-- needed, by the build-time compilers (modules/markup).
package("Velora")
version("0.1.0")
default_config("debug")
default_platform("host")
default_arch("x64")

-- ANTLR4 C++ runtime for build-time generated parsers (RkGen C++ frontend, XAML).
-- Generated with the 4.13.2 tool jar. The 4.13.2 runtime tag misses <chrono> and
-- fails on current MSVC; this dev commit (also pinned by Indigo) contains the fix.
require("antlr4-runtime", {
    source = "https://github.com/antlr/antlr4.git",
    ref = "7d5770395bb7b02eb56e7c62662cb1d7c08f42a3",
    subdir = "runtime/Cpp",
    build = "cmake",
    linkage = "static",
    runtime = "dynamic",
    export_defines = true,
    cmake = {
        generator = "Ninja",
        install = true,
        options = {
            ANTLR_BUILD_CPP_TESTS = false,
            ANTLR_BUILD_SHARED = false,
            ANTLR_BUILD_STATIC = true,
            WITH_DEMO = false,
            WITH_STATIC_CRT = false,
            ANTLR4_INSTALL = false,
            CMAKE_INSTALL_INCLUDEDIR = "include"
        }
    },
    artifacts = {
        include_dirs = {"include/antlr4-runtime"},
        defines = {"ANTLR4CPP_STATIC"}
    },
    platforms = {
        windows = {
            artifacts = {
                lib_files = {
                    debug = {"lib/antlr4-runtime-static.lib"},
                    release = {"lib/antlr4-runtime-static.lib"}
                }
            }
        },
        unix = {
            artifacts = {
                lib_files = {"lib/libantlr4-runtime-static.a"}
            }
        }
    }
})

