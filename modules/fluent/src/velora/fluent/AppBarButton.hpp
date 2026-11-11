// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <velora/fluent/FlyoutButton.hpp>
namespace velora
{
class AppBarButton : public FlyoutButton
{
  public:
    AppBarButton();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& labelProperty();
    static std::string registerType(Framework& ui);
    [[nodiscard]] std::string label() const;
    PropertySetResult setLabel(std::string value);
    [[nodiscard]] std::string iconGlyph() const;
    PropertySetResult setIconGlyph(std::string value);
    [[nodiscard]] bool isCompact() const;
    PropertySetResult setIsCompact(bool value);
    static const PropertyDescriptor& dynamicOverflowOrderProperty();
    [[nodiscard]] i32 dynamicOverflowOrder() const;
    PropertySetResult setDynamicOverflowOrder(i32 value);
    [[nodiscard]] bool isInOverflow() const noexcept override;
    static const PropertyDescriptor& isInOverflowProperty();
    void onDetached() noexcept override;

  protected:
    explicit AppBarButton(Variant variant);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class CommandBar;
    friend class CommandBarFlyout;
    void setOverflowMode(bool value);
};
class AppBarToggleButton final : public AppBarButton
{
  public:
    AppBarToggleButton();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& labelProperty();
    static const PropertyDescriptor& isCheckedProperty();
    static const PropertyDescriptor& dynamicOverflowOrderProperty();
    static std::string registerType(Framework& ui);
    [[nodiscard]] bool isChecked() const;
    PropertySetResult setIsChecked(bool value);
    u64 addIsCheckedChanged(std::function<void(bool)> handler);
};
} // namespace velora
