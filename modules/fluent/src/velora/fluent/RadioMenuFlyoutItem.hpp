// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/MenuFlyout.hpp>

namespace velora
{
// WinUI 2.3 RadioMenuFlyoutItem behavior, using one IsChecked dependency property.
class RadioMenuFlyoutItem final : public MenuFlyoutItem
{
  public:
    RadioMenuFlyoutItem();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    static const PropertyDescriptor& textProperty();
    static const PropertyDescriptor& isCheckedProperty();
    static const PropertyDescriptor& groupNameProperty();
    [[nodiscard]] bool isChecked() const;
    PropertySetResult setIsChecked(bool value);
    [[nodiscard]] std::string groupName() const;
    PropertySetResult setGroupName(std::string value);
    void onAttached(Framework& ui, ElementId self) override;

  protected:
    void normalizePropertyValues(const PropertyDescriptor& property) override;
};
} // namespace velora
