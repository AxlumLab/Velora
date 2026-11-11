// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/PopupPanel.hpp>
namespace velora
{
class TeachingTip final : public PopupPanel
{
  public:
    TeachingTip();
    void attach(Framework&, ElementId target);
    void detach();
    static const PropertyDescriptor& isOpenProperty();
    PropertySetResult setIsOpen(bool);
    std::string subtitle() const;
    PropertySetResult setSubtitle(std::string);
    std::string actionButtonContent() const;
    PropertySetResult setActionButtonContent(std::string);
    std::string closeButtonContent() const;
    PropertySetResult setCloseButtonContent(std::string);
    bool isLightDismissEnabled() const;
    PropertySetResult setIsLightDismissEnabled(bool);
};
} // namespace velora
