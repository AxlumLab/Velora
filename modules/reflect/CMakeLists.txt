# Runtime reflection: reflected types, .rkmeta metadata and generated accessor tables.
velora_module(reflect
    SOURCES velora/reflect/Reflect.cpp velora/reflect/Metadata.cpp velora/reflect/Accessors.cpp
    PUBLIC velora::foundation)
