// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{
struct SelectorPage
{
    std::string header;
    std::shared_ptr<UIElement> content;
};

class PageSelector : public UIElement
{
  public:
    void setPages(std::vector<SelectorPage>);
    const std::vector<SelectorPage>& pages() const noexcept;
    i32 selectedIndex() const;
    PropertySetResult setSelectedIndex(i32);
    std::shared_ptr<UIElement> selectedItem() const;
    const PropertyDescriptor& selectedIndexProperty() const;
    u64 addSelectionChanged(std::function<void(i32, i32)>);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;
    std::string automationName() const override;

  protected:
    explicit PageSelector(bool pivot);
    static const XamlTypeDeclaration& pageDeclaration(std::string_view);
    const PropertyDescriptor& property(std::string_view) const;
    static std::string registerSelector(Framework&, const XamlTypeDeclaration&,
                                        SourceControlRegistry::Factory);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;
    void replacePages(std::vector<SelectorPage>, std::optional<i32> selected);
    virtual bool acceptsPage(const SelectorPage&) const;
    virtual std::string pageHeader(usize index) const;
    virtual ElementId createHeader(Framework&, ElementId parent, usize index);
    virtual void onRebuildingHeaders();
    virtual void onHeadersRebuilt(Framework&, ElementId parent);
    virtual bool hasPivotProperties() const noexcept;
    Framework* attachedFramework() const noexcept;

  private:
    void rebuild();
    void updateSelection();
    void navigate(i32, bool focusHeader);
    bool isLocked() const;
    const bool m_pivot;
    bool m_busy = false;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_headerHost = 0, m_contentHost = 0, m_title = 0;
    ElementId m_previous = 0, m_next = 0;
    std::vector<SelectorPage> m_pages;
    std::vector<ElementId> m_hosts, m_headers;
    std::vector<EventSubscription> m_events;
};
} // namespace velora
