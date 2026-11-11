// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <string>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
namespace velora
{
class NavigationViewItem final : public UIElement
{
  public:
    NavigationViewItem();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    std::string content() const;
    PropertySetResult setContent(std::string);
    static const PropertyDescriptor& iconGlyphProperty();
    std::string iconGlyph() const;
    PropertySetResult setIconGlyph(std::string);
    static const PropertyDescriptor& selectsOnInvokedProperty();
    bool selectsOnInvoked() const;
    PropertySetResult setSelectsOnInvoked(bool);
    static const PropertyDescriptor& isSelectedProperty();
    bool isSelected() const;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;
    std::string automationName() const override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class NavigationView;
    void setPresentation(bool selected, bool compact);
    void updateParts();
    void invoke();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_button = 0;
    bool m_compact = false;
    EventSubscription m_click;
};
} // namespace velora
