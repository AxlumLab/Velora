// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/PageSelector.hpp>
namespace velora
{
class Pivot final : public PageSelector
{
  public:
    Pivot();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    std::string title() const;
    PropertySetResult setTitle(std::string);
    bool isLocked() const;
    PropertySetResult setIsLocked(bool);
};
} // namespace velora
