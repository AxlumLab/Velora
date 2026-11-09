# Dependency properties and runtime type metadata. UI trees, layout and composition integration
# live in velora::controls, which builds on this ordinary C++ library.
velora_module(runtime
    SOURCES
        velora/runtime/property/PropertyRegistry.cpp
        velora/runtime/property/PropertyStore.cpp
        velora/runtime/property/DependencyObject.cpp
        velora/runtime/property/CoreProperties.cpp
        velora/runtime/object/RuntimeTypeRegistry.cpp
        velora/runtime/object/BuiltinTypes.cpp
    PUBLIC velora::foundation)
