#pragma once

#include <string>
#include <vector>
#include <velora/xaml/XamlIR.hpp>
#include <velora/xaml/XamlIRBuilder.hpp>

// velorac's back half of the .vxbf pipeline (R06.4): the C++ that connects a compiled page to its
// code-behind controller. Legacy standalone mode embeds .vxbf; packaged mode references its URI,
// and maps event handler names to controller methods and x:Name entries to typed members.
// It contains no tree construction: the page's structure exists only in the .vxbf.

namespace velora
{

struct CodeBehind
{
    std::string headerName; // "<stem>.g.hpp"
    std::string header, source;
};

// `relativePath` is the XAML file relative to the build's BASE_DIR ("Views/MainPage.xaml"); the
// page's URI is ms-appx:///<relativePath with .vxbf>. `includes` are the controller headers.
// `xbinds` are the page's resolved x:Bind paths (XamlIRResult::xbinds), generated as accessors.
[[nodiscard]] CodeBehind
writeCodeBehind(const XamlIR& ir, const std::vector<u8>& vxbf, std::string_view relativePath,
                const std::vector<std::string>& includes, const std::vector<XamlXBind>& xbinds = {},
                const std::vector<std::pair<std::string, std::string>>& itemHandlers = {},
                bool embedVxbf = true);

} // namespace velora
