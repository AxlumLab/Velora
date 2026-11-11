// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <string_view>
#include <vector>
#include <velora/controls/UIElement.hpp>
namespace velora
{
// One actual content value and one mounted child. Derived classes supply a compiled host template.
class ContentElement : public UIElement
{
  public:
    ContentElement();
    std::shared_ptr<UIElement> content() const;
    PropertySetResult setContent(std::shared_ptr<UIElement>);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    virtual std::string_view contentTemplateUri() const = 0;
    virtual std::string_view contentHostName() const;
    virtual const PropertyDescriptor& hostedContentProperty() const;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    Framework* attachedFramework() const noexcept;
    ElementId contentHost() const noexcept;
    ElementId contentNode() const noexcept;
    ElementId ownerElement() const noexcept;

  private:
    void syncContent();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_host = 0, m_content = 0;
    bool m_syncing = false;
};
} // namespace velora
