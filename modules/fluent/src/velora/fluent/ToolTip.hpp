// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <velora/fluent/Flyout.hpp>

namespace velora
{
// Retain this C++ service while its anchor is in use. It owns no second UI runtime.
class ToolTip final : public DependencyObject, public std::enable_shared_from_this<ToolTip>
{
  public:
    ToolTip();
    ~ToolTip() override;
    static const PropertyDescriptor& contentProperty();
    [[nodiscard]] std::string content() const;
    PropertySetResult setContent(std::string value);
    void attach(Framework&, ElementId anchor);
    void detach();
    bool show();
    void hide();
    [[nodiscard]] bool isOpen() const noexcept;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    Framework* m_ui = nullptr;
    std::weak_ptr<void> m_lifetime;
    ElementId m_anchor = 0;
    std::size_t m_hover = 0;
    std::shared_ptr<Flyout> m_flyout;
};
} // namespace velora
