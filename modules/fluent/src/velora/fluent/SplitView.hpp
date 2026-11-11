// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/PaneLayout.hpp>

namespace velora
{
class SplitView final : public PaneLayout
{
  public:
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& paneObjectProperty();
    static const PropertyDescriptor& isPaneOpenProperty();
    [[nodiscard]] std::shared_ptr<UIElement> content() const;
    PropertySetResult setContent(std::shared_ptr<UIElement>);
    [[nodiscard]] std::shared_ptr<UIElement> pane() const;
    PropertySetResult setPane(std::shared_ptr<UIElement>);
    [[nodiscard]] bool isPaneOpen() const;
    PropertySetResult setIsPaneOpen(bool);
    [[nodiscard]] f64 openPaneLength() const;
    PropertySetResult setOpenPaneLength(f64);
    [[nodiscard]] f64 compactPaneLength() const;
    PropertySetResult setCompactPaneLength(f64);
    [[nodiscard]] std::string displayMode() const;
    PropertySetResult setDisplayMode(std::string);
    [[nodiscard]] std::string panePlacement() const;
    PropertySetResult setPanePlacement(std::string);
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;

  protected:
    const PropertyDescriptor& paneProperty(usize) const override;
    PaneLayoutPlan layoutPanes(Size, const std::array<Size, 2>&) override;
    bool validateLayoutProperty(const PropertyDescriptor&, const PropertyValue&) const override;
};
} // namespace velora
