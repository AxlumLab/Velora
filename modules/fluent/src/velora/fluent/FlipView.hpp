// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/PageSelector.hpp>
namespace velora
{
class FlipView final : public PageSelector
{
  public:
    FlipView();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
};
} // namespace velora
