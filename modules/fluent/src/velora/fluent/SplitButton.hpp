// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/FlyoutButton.hpp>

namespace velora
{
class SplitButton : public FlyoutButton
{
  public:
    SplitButton();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& contentProperty();
    static std::string registerType(Framework& ui);

  protected:
    explicit SplitButton(Variant variant);
};
} // namespace velora
