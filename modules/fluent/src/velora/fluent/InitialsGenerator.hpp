// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from WinUI 2.3 dev/PersonPicture/InitialsGenerator.{h,cpp}; see WinUI23.provenance.json.
#pragma once

#include <string>
#include <string_view>

namespace velora
{
// The WinUI display-name algorithm, with bounded UTF-16 reads and scalar-aware classification.
class InitialsGenerator final
{
  public:
    [[nodiscard]] static std::string fromDisplayName(std::string_view name);
};
} // namespace velora
