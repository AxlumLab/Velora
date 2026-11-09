# Verifies that editing one header regenerates only the metadata that depends on it.
# Inputs: SOURCE_DIR, WORK_DIR, VELORAC, VELORA_CMAKE_DIR, MAKE_PROGRAM.
file(REAL_PATH "${ROOT_DIR}" _root)
file(REAL_PATH "${WORK_DIR}" _work)
string(FIND "${_work}/" "${_root}/" _inside)
if(NOT _inside EQUAL 0 OR _work STREQUAL _root)
    message(FATAL_ERROR "Test workspace must be a child of ROOT_DIR: ${_work}")
endif()
file(REMOVE_RECURSE "${_work}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${WORK_DIR}/src" PATTERN "run.cmake" EXCLUDE)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK_DIR}/src" -B "${WORK_DIR}/build" -G Ninja
        "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DVELORAC_EXECUTABLE=${VELORAC}" "-DVELORA_CMAKE_DIR=${VELORA_CMAKE_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "configure failed:\n${out}")
endif()

# Builds and returns the sorted list of regenerated headers.
function(build_step name expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/build"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${name}: build failed:\n${out}")
    endif()
    string(REGEX MATCHALL "rkmeta [a-z]+" ran "${out}")
    list(SORT ran)
    string(REPLACE ";" "," ran_text "${ran}")
    if(NOT ran_text STREQUAL "${expected}")
        message(FATAL_ERROR "${name}: regenerated [${ran_text}], expected [${expected}]\n${out}")
    endif()
    message(STATUS "${name}: regenerated [${ran_text}]")
endfunction()

build_step("initial build" "rkmeta a,rkmeta b,rkmeta c")
build_step("no change" "")
file(TOUCH "${WORK_DIR}/src/a.hpp")
build_step("touch a.hpp" "rkmeta a")
file(APPEND "${WORK_DIR}/src/macros.hpp" "// edited\n")
build_step("edit include of b.hpp" "rkmeta b")
file(APPEND "${WORK_DIR}/src/c.hpp" "// comment only\n")
build_step("comment-only edit of c.hpp" "rkmeta c")
build_step("no change after edits" "")

file(READ "${WORK_DIR}/build/rkmeta/metadata/b.rkmeta.d" depfile)
if(NOT depfile MATCHES "macros.hpp")
    message(FATAL_ERROR "depfile does not list the scanned include:\n${depfile}")
endif()
message(STATUS "incremental rkmeta generation: PASS")
