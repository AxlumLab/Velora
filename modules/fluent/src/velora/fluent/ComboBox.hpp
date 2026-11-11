// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/ChoiceControl.hpp>
namespace velora
{
class ComboBox final : public ChoiceControl
{
  public:
    ComboBox();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    bool isDropDownOpen() const;
    PropertySetResult setIsDropDownOpen(bool);
    std::string placeholderText() const;
    PropertySetResult setPlaceholderText(std::string);
};
} // namespace velora
