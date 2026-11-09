# XAML object layer: the compiled XAML format (XamlIR, .vxbf reader/writer) and, from R07, the
# XamlLoader that replays a .vxbf through the object runtime.
#
# `xaml` never depends on concrete `controls`: the loader drives an abstract load target that the
# controls module implements, and checks types against the RuntimeTypeRegistry. The format code has
# no ANTLR or XML dependency, so applications link it while velorac (markup) writes it.
velora_module(xaml
    SOURCES velora/xaml/XamlIR.cpp velora/xaml/XamlBinary.cpp velora/xaml/XamlLoader.cpp
    PUBLIC velora::runtime velora::reflect)
