include_guard(GLOBAL)
set(value_pad_types "${CMAKE_CURRENT_LIST_DIR}/ValuePad.vtypes")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${value_pad_types}")
file(READ "${value_pad_types}" VALUEPAD_TYPES)
configure_file("${CMAKE_CURRENT_LIST_DIR}/ValuePadTypes.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/examples/ValuePadTypes.hpp" @ONLY)
