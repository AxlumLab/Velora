// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <velora/fluent/PaneLayout.hpp>

namespace velora
{
enum class TwoPaneViewMode : u8
{
    kSinglePane,
    kWide,
    kTall
};
class TwoPaneView final : public PaneLayout
{
  public:
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& pane1Property();
    static const PropertyDescriptor& pane2Property();
    static const PropertyDescriptor& modeProperty();
    [[nodiscard]] std::shared_ptr<UIElement> pane1() const;
    PropertySetResult setPane1(std::shared_ptr<UIElement>);
    [[nodiscard]] std::shared_ptr<UIElement> pane2() const;
    PropertySetResult setPane2(std::shared_ptr<UIElement>);
    [[nodiscard]] TwoPaneViewMode mode() const;
    u64 addModeChanged(std::function<void(TwoPaneViewMode)>);
    [[nodiscard]] f64 minWideModeWidth() const;
    PropertySetResult setMinWideModeWidth(f64);
    [[nodiscard]] f64 minTallModeHeight() const;
    PropertySetResult setMinTallModeHeight(f64);
    [[nodiscard]] std::string panePriority() const;
    PropertySetResult setPanePriority(std::string);
    [[nodiscard]] std::string wideModeConfiguration() const;
    PropertySetResult setWideModeConfiguration(std::string);
    [[nodiscard]] std::string tallModeConfiguration() const;
    PropertySetResult setTallModeConfiguration(std::string);
    [[nodiscard]] std::string pane1Length() const;
    PropertySetResult setPane1Length(std::string);
    [[nodiscard]] std::string pane2Length() const;
    PropertySetResult setPane2Length(std::string);

  protected:
    const PropertyDescriptor& paneProperty(usize) const override;
    PaneLayoutPlan layoutPanes(Size, const std::array<Size, 2>&) override;
    void onPlanArranged(const PaneLayoutPlan&) override;
    bool validateLayoutProperty(const PropertyDescriptor&, const PropertyValue&) const override;
};
} // namespace velora
