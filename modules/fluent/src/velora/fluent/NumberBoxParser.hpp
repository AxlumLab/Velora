// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
#pragma once
#include <optional>
#include <string_view>
#include <velora/globals.hpp>

namespace velora
{
// Invariant decimal parser; preserves WinUI 2.3's left-associative operators, including ^.
class NumberBoxParser final
{
  public:
    [[nodiscard]] static std::optional<f64> compute(std::string_view text, bool expression = true);
};
} // namespace velora
