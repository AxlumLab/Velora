add_executable(velora_custom_control WIN32 main.cpp ValuePad.cpp)
target_link_libraries(velora_custom_control PRIVATE velora::application velora::controls)
target_compile_definitions(velora_custom_control PRIVATE UNICODE _UNICODE NOMINMAX)
