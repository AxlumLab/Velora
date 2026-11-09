// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/SplitButton.hpp>

namespace velora
{
class ToggleSplitButton final : public SplitButton
{
  public:
    ToggleSplitButton();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& contentProperty();
    static std::string registerType(Framework& ui);
    [[nodiscard]] bool isChecked() const;
    PropertySetResult setIsChecked(bool value);
    static const PropertyDescriptor& isCheckedProperty();
    u64 addIsCheckedChanged(std::function<void(bool)> handler);
};
} // namespace velora
