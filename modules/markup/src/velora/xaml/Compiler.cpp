#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

namespace velora
{

std::string XamlCompiler::readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// C++ string literal; non-ASCII bytes as \x escapes, each closed so following hex digits stay text.
std::string XamlCompiler::cppString(std::string_view s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += static_cast<char>(c);
        }
        else if (c == '\n')
        {
            out += "\\n";
        }
        else if (c < 0x20 || c >= 0x7F)
        {
            out += std::format("\\x{:02X}\"\"", c);
        }
        else
        {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

// C++ expression constructing a PropertyValue. Numbers are printed round-trip exact.
std::string XamlCompiler::cppValue(const PropertyValue& v)
{
    if (v.empty())
        return "velora::PropertyValue()";
    if (v.isBool())
        return std::format("velora::PropertyValue({})", v.asBool());
    if (v.isInt())
        return std::format("velora::PropertyValue(velora::i64{{{}}})", v.asInt());
    if (v.isDouble())
        return std::format("velora::PropertyValue(double{{{}}})", v.asDouble());
    if (v.isString())
        return "velora::PropertyValue(std::string(" + cppString(v.asString()) + "))";
    if (v.isThickness())
    {
        const auto& t = v.asThickness();
        return std::format("velora::PropertyValue(velora::Thickness{{{}, {}, {}, {}}})", t.left, t.top,
                           t.right, t.bottom);
    }
    std::string out = "velora::PropertyValue(velora::GridLengths{";
    for (const auto& g : v.asGridLengths())
    {
        const char* unit = g.unit == GridLength::Unit::kPixel  ? "Pixel"
                           : g.unit == GridLength::Unit::kAuto ? "Auto"
                                                               : "Star";
        out += std::format("{{velora::GridLength::Unit::k{}, {}}}, ", unit, g.value);
    }
    return out + "})";
}

// Framework::Properties member for a schema property ("Grid.Row" -> GridRow).
std::string XamlCompiler::memberOf(std::string_view property)
{
    std::string out(property);
    std::erase(out, '.');
    return out;
}

std::string XamlCompiler::bareType(std::string_view type)
{
    while (type.starts_with("const "))
        type.remove_prefix(6);
    while (!type.empty() && (type.back() == '&' || type.back() == '*' || type.back() == ' '))
        type.remove_suffix(1);
    if (type.ends_with(" const"))
        type.remove_suffix(6);
    return std::string(type);
}

bool XamlCompiler::identifier(std::string_view s)
{
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    return std::ranges::all_of(s, [](char c)
                               { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// Source order; a problem found once per theme is reported once.
void XamlCompiler::sortDiagnostics(std::vector<XamlDiagnostic>& diagnostics)
{
    std::ranges::stable_sort(diagnostics, [](const XamlDiagnostic& a, const XamlDiagnostic& b)
                             { return a.span.begin.offset < b.span.begin.offset; });
    const auto same = [](const XamlDiagnostic& a, const XamlDiagnostic& b)
    { return a.span.begin.offset == b.span.begin.offset && a.message == b.message; };
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end(), same), diagnostics.end());
}

XamlClassName XamlCompiler::className(std::string_view dotted)
{
    XamlClassName out;
    const auto dot = dotted.rfind('.');
    out.name = std::string(dot == std::string_view::npos ? dotted : dotted.substr(dot + 1));
    if (dot != std::string_view::npos)
    {
        out.ns = std::string(dotted.substr(0, dot));
        for (std::size_t i = 0; (i = out.ns.find('.', i)) != std::string::npos;)
            out.ns.replace(i, 1, "::");
    }
    return out;
}

bool XamlCompiler::isPointer(std::string_view type)
{
    return type.find('*') != std::string_view::npos;
}
bool XamlCompiler::isMutableReference(std::string_view type)
{
    return type.ends_with("&") && !type.ends_with("&&") && !type.starts_with("const ") &&
           type.find(" const") == std::string_view::npos;
}

TypeCategory XamlCompiler::category(std::string_view type)
{
    std::string bare = bareType(type);
    if (bare.starts_with("velora::"))
        bare.erase(0, 8);
    static const std::set<std::string_view> strings = {"std::string", "std::string_view", "char",
                                                       "char const"};
    static const std::set<std::string_view> integers = {
        "int",          "long",         "short",         "unsigned",      "long long",   "unsigned int",
        "std::int32_t", "std::int64_t", "std::uint32_t", "std::uint64_t", "std::size_t", "int32_t",
        "int64_t",      "uint32_t",     "uint64_t",      "size_t",        "i8",          "u8",
        "i16",          "u16",          "i32",           "u32",           "i64",         "u64",
        "usize",        "isize",        "uptr",          "iptr"};
    if (strings.contains(bare))
        return (bare == "char" || bare == "char const") && !isPointer(type) ? TypeCategory::kOther
                                                                            : TypeCategory::kString;
    if (integers.contains(bare))
        return TypeCategory::kInteger;
    if (bare == "float" || bare == "double" || bare == "f32" || bare == "f64")
        return TypeCategory::kFloating;
    if (bare == "bool")
        return TypeCategory::kBoolean;
    return TypeCategory::kOther;
}

// Converts a C++ expression of `type` to the PropertyValue a property kind stores.
std::optional<std::string> XamlCompiler::convert(const std::string& expr, std::string_view type,
                                                 ValueKind kind)
{
    const TypeCategory c = category(type);
    switch (kind)
    {
    case ValueKind::kString:
        if (c == TypeCategory::kString)
            return "velora::PropertyValue(std::string(" + expr + "))";
        break;
    case ValueKind::kDouble:
        if (c == TypeCategory::kFloating || c == TypeCategory::kInteger)
            return "velora::PropertyValue(static_cast<double>(" + expr + "))";
        break;
    case ValueKind::kInteger:
    case ValueKind::kBrush:
        if (c == TypeCategory::kInteger)
            return "velora::PropertyValue(static_cast<velora::i64>(" + expr + "))";
        break;
    case ValueKind::kBoolean:
    case ValueKind::kVisibility:
        if (c == TypeCategory::kBoolean)
            return "velora::PropertyValue(static_cast<bool>(" + expr + "))";
        break;
    case ValueKind::kObject:
        // A DataContext names an object the view does not own: it must be an lvalue.
        if (c == TypeCategory::kOther && isMutableReference(type))
            return "velora::PropertyValue(velora::ObjectRef::borrow(" + expr + "))";
        break;
    case ValueKind::kCornerRadius:
        if (c == TypeCategory::kFloating || c == TypeCategory::kInteger)
            return "velora::PropertyValue(static_cast<double>(" + expr + "))";
        break;
    default:
        break;
    }
    return std::nullopt;
}

// T of `velora::ObservableVector<T>`, spelled with or without its namespace.
std::optional<std::string> XamlCompiler::vectorItem(std::string_view type)
{
    std::string bare = bareType(type);
    std::erase(bare, ' ');
    for (const std::string_view prefix : {"velora::ObservableVector<", "ObservableVector<"})
        if (bare.starts_with(prefix) && bare.ends_with(">"))
            return bare.substr(prefix.size(), bare.size() - prefix.size() - 1);
    return std::nullopt;
}

bool XamlCompiler::dottedPath(std::string_view s)
{
    for (std::size_t start = 0;;)
    {
        const auto dot = s.find('.', start);
        if (!identifier(s.substr(start, dot - start)))
            return false;
        if (dot == std::string_view::npos)
            return true;
        start = dot + 1;
    }
}

void XamlCompiler::run(const XamlDocument& document)
{
    loadMetadataAndPeers();

    const XamlElement& root = *document.root;
    for (const auto& a : root.attributes)
        if (a.kind == XamlAttributeKind::kNamespaceDeclaration)
            m_prefixes[a.name.prefix == "xmlns" ? a.name.local : ""] = a.value;
    const auto* xClass = root.attribute(kXamlNs, "Class");
    if (root.name.ns != kXamlPresentationNs ||
        (root.name.local != "Page" && root.name.local != "UserControl"))
    {
        error(root.nameSpan, root.name.local == "ResourceDictionary" && root.name.ns == kXamlPresentationNs
                                 ? "a ResourceDictionary file is not a view; list it under RESOURCES "
                                   "(velorac resources checks it)"
                                 : "the root element must be a Page or a UserControl");
        return;
    }
    if (!xClass)
    {
        error(root.nameSpan, "the root element needs x:Class naming its C++ controller");
        return;
    }
    m_class = className(xClass->value);
    m_controller = m_catalog.find(m_class.qualified());
    if (!m_controller || m_controller->qualifiedName != m_class.qualified())
    {
        error(xClass->valueSpan, "x:Class '" + xClass->value + "': no reflection metadata for C++ class " +
                                     m_class.qualified() +
                                     " (annotate it with rkclass and pass its .rkmeta)");
        return;
    }
    useType(m_class.qualified());
    m_page.cls = m_class.name + "_xaml";
    m_page.rootType = m_class.qualified();
    m_page.rootExpr = "m_controller";
    element(root, "parent");
    m_result.headerName = m_path.stem().string() + ".g.hpp";
    if (m_result.ok())
        emit();
    m_result.dependencies.assign(m_dependencies.begin(), m_dependencies.end());
}

std::string XamlCompiler::resolveTypeName(std::string_view prefixed)
{
    const auto colon = prefixed.find(':');
    const std::string prefix(colon == std::string_view::npos ? "" : prefixed.substr(0, colon));
    const auto it = m_prefixes.find(prefix);
    if (it == m_prefixes.end() || !it->second.starts_with("using:"))
        return {};
    return className(it->second.substr(6) + "." + std::string(prefixed.substr(colon + 1))).qualified();
}

// ---- Elements -------------------------------------------------------------------------

void XamlCompiler::element(const XamlElement& e, const std::string& parentExpr)
{
    if (e.ignored)
        return;
    const std::size_t index = m_scope->elements++;
    const std::string here = elementExpr(index);

    if (e.name.ns.starts_with("using:"))
    {
        const std::string qualified = className(e.name.ns.substr(6) + "." + e.name.local).qualified();
        const auto* type = m_catalog.find(qualified);
        if (!type || type->qualifiedName != qualified)
        {
            error(e.nameSpan, "unknown type '" + e.name.prefix + ":" + e.name.local +
                                  "' (no reflection metadata for " + qualified + ")");
            return;
        }
        if (m_scope->isControlTemplate())
        {
            error(e.nameSpan, "custom controls are not supported inside a ControlTemplate");
            return;
        }
        customElement(e, index, *type, parentExpr);
        return;
    }
    const auto* type = e.name.ns == kXamlPresentationNs ? findSchemaType(e.name.local) : nullptr;
    if (!type)
    {
        error(e.nameSpan, "'" + e.name.local + "' is not supported by the Velora runtime" +
                              (e.name.ns == kXamlNs ? " outside a resource dictionary" : ""));
        return;
    }
    if (!e.name.condition.empty())
        error(e.nameSpan, "conditional XAML namespaces are not supported by velorac");
    if (type->name == "ContentPresenter" && !m_scope->isControlTemplate())
        error(e.nameSpan, "ContentPresenter is only valid inside a ControlTemplate");

    XamlDictionary* const outerDictionary = m_dictionary;
    elementResources(e);
    m_scope->build.push_back(
        line(e.nameSpan) +
        std::format("    {} = m_ui.create(velora::ElementKind::k{}, {});\n", here, type->name, parentExpr));
    std::set<std::string> assigned;
    const XamlAttribute* itemsSource = nullptr;
    for (const auto& a : e.attributes)
    {
        if (a.kind == XamlAttributeKind::kProperty && (a.name.local == "Style" || a.name.local == "Template"))
            continue; // below
        if (type->items && a.kind == XamlAttributeKind::kProperty && a.name.local == "ItemsSource")
            itemsSource = &a;
        else
            setProperty(index, type, a, assigned);
    }

    std::vector<const XamlElement*> objects;
    const XamlElement* itemTemplate = nullptr;
    std::string text;
    std::optional<Span> textSpan;
    for (const auto& child : e.children)
    {
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child))
        {
            const XamlElement& c = **nested;
            if (c.ignored)
                continue;
            if (c.propertyElement && type->items && c.owner == type->name && c.property == "ItemTemplate")
                itemTemplate = &c;
            else if (c.propertyElement && c.owner == type->name &&
                     (c.property == "Style" || c.property == "Template"))
                continue; // below
            else if (c.propertyElement && c.owner == "VisualStateManager" &&
                     c.property == "VisualStateGroups")
            {
                if (!m_scope->isControlTemplate())
                    error(c.nameSpan, "VisualStateManager.VisualStateGroups is supported inside a "
                                      "ControlTemplate in this subset");
                else if (m_scope->visualStateGroups)
                    error(c.nameSpan, "a ControlTemplate has one VisualStateManager.VisualStateGroups");
                else
                    m_scope->visualStateGroups = &c;
            }
            else if (c.propertyElement)
                propertyElement(index, *type, c, assigned);
            else
                objects.push_back(&c);
        }
        else if (const auto* t = std::get_if<XamlText>(&child))
        {
            text += t->value;
            if (!textSpan)
                textSpan = t->span;
        }
    }
    // Inside a ControlTemplate, a ContentPresenter shows the templated parent's content.
    if (type->name == "ContentPresenter" && m_scope->isControlTemplate())
        for (const char* implicit : {"Content", "Foreground", "FontSize"})
            if (!assigned.contains(implicit))
                m_scope->build.push_back(
                    std::format("    m_ui.templateBind({0}, p.{1}, m_owner, p.{1});\n", here, implicit));

    const auto content = type->content;
    if (textSpan && content != ContentModel::kText)
        error(*textSpan, "text content is not allowed in '" + std::string(type->name) + "'");
    if (textSpan && content == ContentModel::kText)
    {
        const std::string property(type->contentProperty);
        if (!assigned.insert(property).second)
            error(*textSpan, "'" + property + "' is set both as an attribute and as content");
        else
            m_scope->build.push_back(line(*textSpan) + std::format("    m_ui.set({}, p.{}, {});\n", here,
                                                                   memberOf(property),
                                                                   cppValue(PropertyValue(text))));
    }
    // Style setters first (lowest precedence among these), then the template, then children.
    const auto [styleTemplate, styleHome] = applyStyle(e, index, *type);
    const XamlElement* controlTemplate = styleTemplate;
    XamlDictionary* templateHome = styleHome;
    for (const auto& child : e.children)
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && (*nested)->propertyElement && (*nested)->owner == type->name &&
            (*nested)->property == "Template")
        {
            const XamlElement* found = nullptr;
            for (const auto& g : (*nested)->children)
                if (const auto* t = std::get_if<std::shared_ptr<XamlElement>>(&g); t && !(*t)->ignored)
                    found = t->get();
            if (!found || found->name.local != "ControlTemplate")
                error((*nested)->nameSpan,
                      "'" + std::string(type->name) + ".Template' contains a ControlTemplate");
            else
                controlTemplate = found, templateHome = m_dictionary;
        }
    if (const auto* a = e.attribute("", "Template"))
    {
        if (!a->markup || (a->markup->kind != "StaticResource" && a->markup->kind != "ThemeResource"))
        {
            error(a->valueSpan, "Template takes a {StaticResource} naming a ControlTemplate");
        }
        else
        {
            const auto& key =
                a->markup->positional ? *a->markup->positional : *a->markup->argument("ResourceKey");
            XamlResource* r = lookup(key.value.text, m_dictionary, 0);
            if (!r)
                error(key.value.span, "resource '" + key.value.text + "' is not defined in this scope");
            else if (r->type != XamlResource::Type::kTemplate)
                error(key.value.span, "resource '" + key.value.text + "' is not a ControlTemplate");
            else
                controlTemplate = r->element, templateHome = r->home;
        }
    }
    if (controlTemplate)
        instantiateTemplate(e, index, *type, *controlTemplate, templateHome);

    if (!objects.empty() && (content == ContentModel::kNone || content == ContentModel::kText))
        error(objects.front()->nameSpan,
              "'" + std::string(type->name) + "' cannot contain elements" +
                  (type->items ? " (items come from ItemsSource and ItemTemplate)" : ""));
    else if (objects.size() > 1 && content == ContentModel::kSingle)
        error(objects[1]->nameSpan, "'" + std::string(type->name) + "' can have only one child element");
    else
        for (const XamlElement* c : objects)
            element(*c, here);
    if (type->items)
        items(e, index, itemsSource, itemTemplate);
    m_dictionary = outerDictionary;
}

} // namespace velora
