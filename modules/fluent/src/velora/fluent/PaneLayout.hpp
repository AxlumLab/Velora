// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{
struct PaneLayoutPlan
{
    Size size;
    std::array<Box, 2> panes{};
    std::array<bool, 2> visible{true, true};
    bool dismissOverlay = false;
    std::string_view mode;
};

// Shared hosting of actual content objects. Slot values live exclusively in dependency properties.
class PaneLayout : public UIElement
{
  public:
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;

  protected:
    PaneLayout();
    virtual const PropertyDescriptor& paneProperty(usize index) const = 0;
    virtual PaneLayoutPlan layoutPanes(Size, const std::array<Size, 2>& desired) = 0;
    virtual void onPlanArranged(const PaneLayoutPlan&)
    {
    }
    virtual bool validateLayoutProperty(const PropertyDescriptor&, const PropertyValue&) const = 0;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    std::shared_ptr<UIElement> paneObject(usize index) const;
    PropertySetResult setPaneObject(usize index, std::shared_ptr<UIElement> value);
    static const XamlTypeDeclaration& paneDeclaration(std::string_view name);
    static const PropertyDescriptor& layoutProperty(std::string_view type, std::string_view name);
    static std::string registerLayout(Framework&, const XamlTypeDeclaration&,
                                      std::function<std::shared_ptr<UIElement>()>);
    Framework* attachedFramework() const noexcept;
    ElementId paneHost(usize index) const noexcept;

  private:
    void syncPanes();
    std::array<Size, 2> measurePanes(LayoutContext&, Size);
    void applyPlan(const PaneLayoutPlan&);
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_root = 0, m_overlay = 0;
    std::array<ElementId, 2> m_hosts{}, m_items{};
    bool m_syncing = false;
};
} // namespace velora
