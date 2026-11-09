#include <array>
#include <string>
#include <vector>
#include <velora/controls/ControlTypes.hpp>
#include <velora/runtime/object/BuiltinTypes.hpp>

namespace velora
{

RegistrationResult registerControlTypes(RuntimeTypeRegistry& registry)
{
    struct Type
    {
        const char* name;
        const char* base;
    };
    // UI type hierarchy shared by controls and the compiled XAML loader.
    static constexpr Type kTypes[] = {
        {"Velora.UI.Panel", kFrameworkElementClassName},
        {"Velora.UI.StackPanel", "Velora.UI.Panel"},
        {"Velora.UI.Grid", "Velora.UI.Panel"},
        {"Velora.UI.Canvas", "Velora.UI.Panel"},
        {"Velora.UI.Border", kFrameworkElementClassName},
        {"Velora.UI.TextBlock", kFrameworkElementClassName},
        {"Velora.UI.Image", kFrameworkElementClassName},
        {"Velora.UI.ContentPresenter", kFrameworkElementClassName},
        {"Velora.UI.ContentControl", kControlClassName},
        {"Velora.UI.UserControl", "Velora.UI.ContentControl"},
        {"Velora.UI.Page", "Velora.UI.UserControl"},
        {"Velora.UI.ScrollViewer", "Velora.UI.ContentControl"},
        {"Velora.UI.ButtonBase", "Velora.UI.ContentControl"},
        {"Velora.UI.Button", "Velora.UI.ButtonBase"},
        {"Velora.UI.ToggleButton", "Velora.UI.ButtonBase"},
        {"Velora.UI.CheckBox", "Velora.UI.ToggleButton"},
        {"Velora.UI.RadioButton", "Velora.UI.ToggleButton"},
        {"Velora.UI.TextBox", kControlClassName},
        {"Velora.UI.ToggleSwitch", kControlClassName},
        {"Velora.UI.RangeBase", kControlClassName},
        {"Velora.UI.Slider", "Velora.UI.RangeBase"},
        {"Velora.UI.ProgressBar", "Velora.UI.RangeBase"},
        {"Velora.UI.ItemsControl", kControlClassName},
        {"Velora.UI.ListView", "Velora.UI.ItemsControl"},
    };
    std::vector<RuntimeTypeRegistry::Entry> entries;
    entries.reserve(std::size(kTypes));
    for (const auto& type : kTypes)
        entries.push_back({typeIdFor(type.name), type.name, typeIdFor(type.base)});
    return registry.registerTypes(entries);
}

} // namespace velora
