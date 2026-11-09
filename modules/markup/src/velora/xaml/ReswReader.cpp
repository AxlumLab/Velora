#include <algorithm>
#include <fstream>
#include <iterator>
#include <velora/xaml/ReswReader.hpp>
#include <velora/xaml/Parser.hpp>

namespace velora
{

namespace
{
const XamlElement* childElement(const XamlElement& element, std::string_view name)
{
    for (const auto& node : element.children)
        if (const auto* child = std::get_if<std::shared_ptr<XamlElement>>(&node);
            child && (*child)->name.local == name)
            return child->get();
    return nullptr;
}

std::string textOf(const XamlElement& element)
{
    std::string text;
    for (const auto& node : element.children)
        if (const auto* t = std::get_if<XamlText>(&node))
            text += t->value;
    return text;
}
} // namespace

ReswResult readResw(std::string_view source)
{
    ReswResult result;
    XamlParseOptions options;
    options.isKnownType = [](const QName&) { return true; }; // a .resw is plain XML, not XAML
    options.plainXml = true;
    auto parsed = parseXaml(source, options);
    if (!parsed.ok())
    {
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }
    const auto& root = parsed.document.root;
    if (!root || root->name.local != "root")
    {
        result.diagnostics.push_back({root ? root->nameSpan : Span{}, "a .resw file has a <root> element"});
        return result;
    }
    for (const auto& node : root->children)
    {
        const auto* child = std::get_if<std::shared_ptr<XamlElement>>(&node);
        if (!child || (*child)->name.local != "data")
            continue; // resheader, xsd:schema, metadata, assembly: tooling information
        const XamlElement& data = **child;
        const XamlAttribute* name = data.attribute("", "name");
        if (!name || name->value.empty())
        {
            result.diagnostics.push_back({data.nameSpan, "<data> without a name"});
            continue;
        }
        if (data.attribute("", "type") || data.attribute("", "mimetype"))
        {
            result.diagnostics.push_back(
                {data.nameSpan, "'" + name->value + "': only string resources are supported in .resw files"});
            continue;
        }
        const XamlElement* value = childElement(data, "value");
        result.entries.push_back({name->value, value ? textOf(*value) : std::string()});
    }
    return result;
}

std::variant<ResourceIndex, std::string> buildResourceIndex(const std::filesystem::path& root,
                                                            std::string name, std::string defaultLanguage)
{
    namespace fs = std::filesystem;
    auto scanned = indexResourceFiles(root, std::move(name), std::move(defaultLanguage));
    if (auto* error = std::get_if<ResourceScanError>(&scanned))
        return std::move(error->message);
    auto index = std::move(std::get<ResourceIndex>(scanned));

    std::vector<fs::path> tables;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error) && it->path().extension() == ".resw")
            tables.push_back(it->path());
    if (error)
        return "cannot list '" + root.generic_string() + "': " + error.message();
    std::ranges::sort(tables);
    for (const auto& table : tables)
    {
        const std::string relative = table.lexically_relative(root).generic_string();
        std::string language = table.parent_path().filename().string();
        if (language.starts_with("lang-"))
            language.erase(0, 5);
        if (table.parent_path() == root || !parseQualifiers("lang-" + language))
            return relative + ": a .resw file goes in a language directory (Strings/en-US/Resources.resw)";
        std::ifstream in(table, std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(in), {}};
        if (!in && !in.eof())
            return relative + ": cannot read";
        const auto resw = readResw(source);
        if (!resw.ok())
            return formatDiagnostic(resw.diagnostics.front(), relative);
        if (auto message = addReswStrings(index, table.stem().string(), language, resw.entries);
            !message.empty())
            return relative + ": " + message;
    }
    if (auto message = index.validate(); !message.empty())
        return message;
    return index;
}

} // namespace velora
