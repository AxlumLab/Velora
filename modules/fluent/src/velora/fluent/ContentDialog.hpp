// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/PopupPanel.hpp>
namespace velora
{
class ContentDialog final : public PopupPanel
{
  public:
    ContentDialog();
    bool showAt(Framework&, ElementId anchor);
    std::string primaryButtonText() const;
    PropertySetResult setPrimaryButtonText(std::string);
    std::string secondaryButtonText() const;
    PropertySetResult setSecondaryButtonText(std::string);
    std::string closeButtonText() const;
    PropertySetResult setCloseButtonText(std::string);
    std::string defaultButton() const;
    PropertySetResult setDefaultButton(std::string);
    bool isPrimaryButtonEnabled() const;
    PropertySetResult setIsPrimaryButtonEnabled(bool);
    bool isSecondaryButtonEnabled() const;
    PropertySetResult setIsSecondaryButtonEnabled(bool);
};
} // namespace velora
