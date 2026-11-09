// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/FlyoutButton.hpp>

namespace velora
{
class DropDownButton final : public FlyoutButton
{
  public:
    DropDownButton();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& contentProperty();
    static std::string registerType(Framework& ui);
};
} // namespace velora
