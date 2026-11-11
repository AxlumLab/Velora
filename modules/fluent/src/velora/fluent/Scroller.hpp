// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <velora/controls/ContentElement.hpp>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
// WinUI 2.3's preview Scroller scroll-only slice, backed by the real Velora ScrollViewer.
class Scroller final : public ContentElement
{
  public:
    Scroller();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    f64 horizontalOffset() const;
    f64 verticalOffset() const;
    f64 extentWidth() const;
    f64 extentHeight() const;
    f64 viewportWidth() const;
    f64 viewportHeight() const;
    bool scrollTo(f64 x, f64 y);
    bool scrollBy(f64 x, f64 y);
    ElementId scrollViewer() const noexcept;
    u64 addViewChanged(std::function<void(f64, f64)>);
    bool removeViewChanged(u64);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    std::string_view contentTemplateUri() const override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    f64 result(const PropertyDescriptor*) const;
    void syncModes();
    EventSubscription m_viewChanged;
    std::map<u64, std::function<void(f64, f64)>> m_handlers;
    u64 m_nextToken = 1;
};
} // namespace velora
