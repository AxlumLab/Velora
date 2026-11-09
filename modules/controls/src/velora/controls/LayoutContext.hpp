#pragma once

#include <string_view>
#include <velora/controls/UIElement.hpp>
#include <velora/runtime/Framework.hpp>

// The services a control's measure/arrange override may use (R05.2). A thin view of Framework:
// recursive measure/arrange of children, desired sizes, property reads, text measurement and the
// few pieces of per-element layout state (list viewports) that Framework owns.

namespace velora
{

class LayoutContext final
{
public:
    explicit LayoutContext(Framework& ui) noexcept : m_ui(ui) {}

    Size measure(ElementId child, Size available);
    void arrange(ElementId child, Box slot);
    [[nodiscard]] Size desired(ElementId child) const;

    [[nodiscard]] const Framework::Properties& props() const noexcept
    {
        return m_ui.props();
    }
    [[nodiscard]] const PropertyValue& property(ElementId element, const PropertyDescriptor* property) const;
    [[nodiscard]] double number(ElementId element, const PropertyDescriptor* property) const;
    [[nodiscard]] std::string_view text(ElementId element, const PropertyDescriptor* property) const;
    // A value stored in some layer (not just the default).
    [[nodiscard]] bool isSet(ElementId element, const PropertyDescriptor* property) const;
    // ScrollViewer axis whose ScrollBarVisibility is not Disabled.
    [[nodiscard]] bool scrollable(ElementId element, const PropertyDescriptor* property) const;
    [[nodiscard]] float measureText(std::string_view text, float fontSize) const;
    [[nodiscard]] ElementId contentObject(ElementId element) const;
    // Writes layout results (offsets, extents) as properties.
    void set(ElementId element, const PropertyDescriptor* property, PropertyValue value);
    // The ListView viewport Framework reports through onViewport.
    [[nodiscard]] Framework::Viewport& listViewport(ElementId list);

private:
    Framework& m_ui;
};

} // namespace velora
