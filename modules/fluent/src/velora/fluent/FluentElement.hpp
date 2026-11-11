// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/runtime/Framework.hpp>
namespace velora
{
const XamlTypeDeclaration& fluentDeclaration(std::string_view name);
const PropertyDescriptor& fluentProperty(std::string_view type, std::string_view name);
std::string registerFluentControl(Framework&, std::string_view type, SourceControlRegistry::Factory);
// Common compiled-template plumbing; each derived control owns its real properties and behavior.
class FluentElement : public UIElement
{
  public:
    std::string className() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    explicit FluentElement(std::string_view type, std::string_view view);
    const PropertyDescriptor& property(std::string_view) const;
    ElementId part(std::string_view) const;
    void bindClick(std::string_view, std::function<void(FluentElement&)>);
    virtual void syncParts() = 0;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_root = 0;
    std::vector<EventSubscription> m_events;

  private:
    const std::string m_type, m_view;
    std::unordered_map<std::string, ElementId> m_parts;
};
} // namespace velora
