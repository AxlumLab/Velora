// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
class ItemsRepeater final : public FluentElement
{
  public:
    // The factory receives an optional recycled object and must return an unattached UIElement.
    using ElementFactory = std::function<std::shared_ptr<UIElement>(i32, std::shared_ptr<UIElement>)>;
    ItemsRepeater();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    void setItems(std::vector<std::string>);
    void setItemsSource(i32 count, ElementFactory);
    i32 itemCount() const noexcept;
    usize realizedCount() const noexcept;
    std::shared_ptr<UIElement> tryGetElement(i32) const;
    std::shared_ptr<UIElement> getOrCreateElement(i32);
    i32 getElementIndex(const UIElement&) const;
    void setRealizationWindow(f64 offset, f64 height);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void clearRealized(bool recycle);
    void realize();
    std::shared_ptr<UIElement> createAt(i32);
    void updateWindow();
    f64 stride() const;
    ElementFactory m_factory;
    i32 m_count = 0;
    f64 m_offset = 0, m_viewport = 240;
    bool m_realizing = false;
    std::map<i32, ElementId> m_realized;
    std::vector<std::shared_ptr<UIElement>> m_pool;
    std::weak_ptr<UIElement> m_scrollSource;
    u64 m_viewportToken = 0;
    EventSubscription m_viewChanged;
};
} // namespace velora
