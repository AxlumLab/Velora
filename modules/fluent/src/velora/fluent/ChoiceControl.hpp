// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/fluent/MenuFlyout.hpp>

namespace velora
{
// String items are the initial collection API. Selection lives in one dependency property.
class ChoiceControl : public UIElement
{
  public:
    void setItems(std::vector<std::string>);
    const std::vector<std::string>& items() const noexcept;
    i32 selectedIndex() const;
    PropertySetResult setSelectedIndex(i32);
    std::string selectedItem() const;
    std::shared_ptr<UIElement> containerFromIndex(i32) const;
    std::string header() const;
    PropertySetResult setHeader(std::string);
    // Remove with removePropertyChanged. Includes C++, XAML and effective-value changes.
    u64 addSelectionChanged(std::function<void(i32, i32)>);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;
    std::string automationName() const override;
    const PropertyDescriptor& selectedIndexProperty() const;

  protected:
    explicit ChoiceControl(bool combo);
    static const XamlTypeDeclaration& choiceDeclaration(std::string_view);
    const PropertyDescriptor& property(std::string_view) const;
    static std::string registerChoice(Framework&, const XamlTypeDeclaration&, SourceControlRegistry::Factory);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    void rebuild();
    void updateParts();
    void syncDropDown();
    const bool m_combo;
    bool m_updating = false, m_opening = false;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_header = 0, m_host = 0;
    std::vector<std::string> m_items;
    std::vector<ElementId> m_containers;
    std::vector<EventSubscription> m_events;
    std::shared_ptr<MenuFlyout> m_menu;
};
} // namespace velora
