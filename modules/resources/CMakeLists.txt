# Resource identity and providers (R08): URI normalization, qualifier ranking, the .vpri index, the
# .vpak container, and the loose/embedded/package providers mounted by the ResourceManager. This
# module holds no XAML objects: a ResourceDictionary is XAML and belongs to velora::xaml. It parses
# no XML either: .resw files are compiled into the index by velorac.
velora_module(resources
    SOURCES
        velora/resources/ResourceUri.cpp
        velora/resources/ResourceQualifier.cpp
        velora/resources/ResourceIndex.cpp
        velora/resources/ResourceScan.cpp
        velora/resources/Deflate.cpp
        velora/resources/PackageFile.cpp
        velora/resources/ResourceProvider.cpp
    PUBLIC velora::foundation)
