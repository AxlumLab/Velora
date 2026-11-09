# Controls: the UI tree, layout, input, painting, bindings, styles/templates and the built-in
# controls, on top of the object runtime and property system (velora::runtime).
#
# Framework/ElementTree/VisualBridge retain the velora/runtime/ include path for the XAML
# pipeline. UIElement and its concrete layout implementations live under velora/controls/.
velora_module(controls
    SOURCES
        velora/controls/UIElement.cpp
        velora/controls/ElementFactory.cpp
        velora/controls/EventSubscription.cpp
        velora/controls/ControlTypes.cpp
        velora/controls/ElementSchema.cpp
        velora/controls/XamlComponent.cpp
        velora/controls/XamlItems.cpp
        velora/controls/Automation.cpp
        velora/controls/Panels.cpp
        velora/controls/ContentElements.cpp
        velora/controls/TextAndFixedElements.cpp
        velora/controls/ToggleElements.cpp
        velora/controls/RangeElements.cpp
        velora/runtime/ElementTree.cpp
        velora/runtime/VisualBridge.cpp
        velora/runtime/SchemaValue.cpp
        velora/runtime/PropertyMetadata.cpp
        velora/runtime/BindingEngine.cpp
        velora/runtime/FrameworkContent.cpp
        velora/runtime/FrameworkResources.cpp
        velora/runtime/Framework.cpp
        velora/runtime/FrameworkLayout.cpp
        velora/runtime/FrameworkInput.cpp
        velora/runtime/FrameworkControls.cpp
        velora/runtime/FrameworkScroll.cpp
        velora/runtime/FrameworkPaint.cpp
        velora/runtime/FrameworkStyles.cpp
    PUBLIC velora::xaml velora::resources velora::runtime velora::reflect velora::composition)
