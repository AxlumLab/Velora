// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
struct HsvColor
{
    f64 hue = 0, saturation = 0, value = 0;
};
HsvColor rgbToHsv(u32 argb);
u32 hsvToArgb(HsvColor, f64 alpha = 1);
class ColorSpectrum final : public UIElement
{
  public:
    ColorSpectrum();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& colorProperty();
    u32 color() const;
    PropertySetResult setColor(u32);
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    bool renderOverride(Canvas&, Size) const override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    void onDetached() noexcept override;
    bool isFocusable() const noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void pick(InputContext&, const PointerEventArgs&);
    bool m_dragging = false;
};
class ColorPicker final : public FluentElement
{
  public:
    ColorPicker();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& colorProperty();
    u32 color() const;
    PropertySetResult setColor(u32);
    PropertySetResult setHsv(HsvColor, f64 alpha = 1);
    std::string hex() const;
    bool setHex(std::string_view);
    u64 addColorChanged(std::function<void(u32, u32)>);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void commitHex();
    void channelChanged(usize);
    std::shared_ptr<ColorSpectrum> m_spectrum;
    std::vector<std::pair<std::weak_ptr<UIElement>, u64>> m_listeners;
    bool m_syncing = false;
};
} // namespace velora
