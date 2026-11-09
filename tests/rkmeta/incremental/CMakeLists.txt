# Generation-only project used by run.cmake; it never compiles the generated sources.
cmake_minimum_required(VERSION 3.25)
project(rkmeta_incremental LANGUAGES NONE)
include("${VELORA_CMAKE_DIR}/VeloraReflect.cmake")
add_custom_target(metadata ALL)
velora_reflect_generate(metadata INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" HEADERS a.hpp b.hpp c.hpp)
