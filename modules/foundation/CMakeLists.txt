# Core types, memory resources, change notification and XAML type identities.
velora_module(foundation
    SOURCES
        velora/memory/Resource.cpp
        velora/io/BinaryStream.cpp
        velora/text/Encoding.cpp
        velora/memory/CheckSum.cpp
        velora/types/TypeId.cpp)
target_include_directories(velora_foundation PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>)
