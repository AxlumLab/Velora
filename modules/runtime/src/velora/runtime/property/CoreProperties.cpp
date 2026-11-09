#include <velora/runtime/property/CoreProperties.hpp>

namespace velora
{

const CoreProperties& coreProperties()
{
    static PropertyRegistry registry;
    static const CoreProperties properties = []
    {
        using F = PropertyFlags;
        CoreProperties p;
        // Defaults and flags as in VisualProperties::define and the framework schema.
        p.isVisible =
            &registry.define("IsVisible", PropertyValue(true), F::kMeasure | F::kArrange | F::kPaint);
        p.opacity = &registry.define("Opacity", PropertyValue(1.0), F::kPaint);
        p.width = &registry.define("Width", PropertyValue(), F::kMeasure);
        p.isEnabled = &registry.define("IsEnabled", PropertyValue(true), F::kPaint);
        p.content =
            &registry.define("Content", PropertyValue(std::string()), F::kMeasure); // schema String ""
        return p;
    }();
    return properties;
}

} // namespace velora
