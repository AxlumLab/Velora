// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/ChoiceControl.hpp>
namespace velora
{
class RadioButtons final : public ChoiceControl
{
  public:
    RadioButtons();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    i32 maxColumns() const;
    PropertySetResult setMaxColumns(i32);
};
} // namespace velora
