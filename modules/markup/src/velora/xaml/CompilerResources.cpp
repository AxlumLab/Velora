#include <velora/globals.hpp>
// velorac: resource dictionaries (merged and theme dictionaries), StaticResource and
// ThemeResource resolution per theme, styles, ControlTemplate instances and visual states.
#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace velora
{

bool XamlCompiler::presentation(const XamlElement& e, std::string_view local)
{
    return e.name.ns == kXamlPresentationNs && e.name.local == local && e.name.condition.empty();
}

std::vector<const XamlElement*> XamlCompiler::objectChildren(const XamlElement& e)
{
    std::vector<const XamlElement*> out;
    for (const auto& child : e.children)
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && !(*nested)->ignored)
            out.push_back(nested->get());
    return out;
}

std::string XamlCompiler::textOf(const XamlElement& e)
{
    std::string out;
    for (const auto& child : e.children)
        if (const auto* t = std::get_if<XamlText>(&child))
            out += t->value;
    return out;
}

const MarkupArgument& XamlCompiler::resourceKey(const MarkupExtension& m)
{
    return m.positional ? *m.positional : *m.argument("ResourceKey");
}

// "Background", "(Border.Background)" or "(Border.Background).(SolidColorBrush.Color)" -> Background.
std::string XamlCompiler::targetProperty(std::string path)
{
    if (const auto at = path.find(").(SolidColorBrush.Color)"); at != std::string::npos)
        path = path.substr(0, at + 1);
    if (path.size() > 2 && path.front() == '(' && path.back() == ')')
    {
        path = path.substr(1, path.size() - 2);
        if (const auto dot = path.rfind('.'); dot != std::string::npos)
            path = path.substr(dot + 1);
    }
    return path;
}

bool XamlCompiler::numeric(ValueKind k)
{
    return k == ValueKind::kDouble || k == ValueKind::kCornerRadius;
}

void XamlCompiler::errorIn(const XamlDictionary* home, const Span& span, const Span& reference,
                           const std::string& message)
{
    if (home && !home->file.empty())
        error(reference,
              std::format("{}:{}:{}: {}", home->file, span.begin.line, span.begin.column, message));
    else
        error(span, message);
}

void XamlCompiler::useType(std::string_view qualified)
{
    if (const auto it = m_typeFiles.find(qualified); it != m_typeFiles.end())
    {
        m_includes.insert(it->second.first);
        m_dependencies.insert(it->second.second);
    }
}

void XamlCompiler::loadMetadataAndPeers()
{
    for (const auto& [file, include] : m_options.metadata)
    {
        const auto parsed = parseMetadata(readFile(file));
        if (!parsed.ok())
        {
            error({}, file.generic_string() + ": " + parsed.error);
            continue;
        }
        m_catalog.add(parsed.document);
        for (const auto& t : parsed.document.types)
            m_typeFiles.emplace(t.qualifiedName, std::pair{include, file});
    }
    for (const auto& peer : m_options.peers)
    {
        auto parsed = parseXaml(readFile(peer));
        if (!parsed.document.root)
            continue;
        if (presentation(*parsed.document.root, "ResourceDictionary"))
        {
            m_dictionaryFiles[peer.filename().string()] = DictionaryFile{peer, std::move(parsed)};
        }
        else if (const auto* c = parsed.document.root->attribute(kXamlNs, "Class"))
        {
            const XamlClassName name = className(c->value);
            m_views[name.qualified()] = XamlView{peer.stem().string() + ".g.hpp", name, peer};
        }
    }
    if (!m_options.application.empty())
    {
        m_application = newDictionary(nullptr);
        for (const auto& file : m_options.application)
        {
            if (!m_dictionaryFiles.contains(file.filename().string()))
                m_dictionaryFiles[file.filename().string()] = DictionaryFile{file, parseXaml(readFile(file))};
            if (XamlDictionary* d = dictionaryFile(file.filename().string(), {}))
                m_application->merged.push_back(d);
        }
    }
}

// ---- Dictionaries ---------------------------------------------------------------------

XamlDictionary* XamlCompiler::newDictionary(XamlDictionary* parent, std::string file)
{
    auto d = std::make_unique<XamlDictionary>();
    d->parent = parent;
    d->file = std::move(file);
    return m_dictionaryStore.emplace_back(std::move(d)).get();
}

XamlDictionary* XamlCompiler::dictionaryFile(const std::string& source, const Span& reference)
{
    const std::string name = fs::path(source).filename().string();
    const auto it = m_dictionaryFiles.find(name);
    if (it == m_dictionaryFiles.end())
    {
        error(reference,
              "ResourceDictionary '" + source + "' is not part of this build (list it under RESOURCES)");
        return nullptr;
    }
    auto& f = it->second;
    if (f.loading)
    {
        error(reference, "MergedDictionaries cycle through '" + name + "'");
        return nullptr;
    }
    if (f.dictionary)
        return f.dictionary;
    m_dependencies.insert(f.path);
    if (!f.parsed->ok())
    {
        const auto& d = f.parsed->diagnostics.front();
        error(reference,
              std::format("{}:{}:{}: {}", name, d.span.begin.line, d.span.begin.column, d.message));
        return nullptr;
    }
    f.loading = true;
    XamlDictionary* d = newDictionary(nullptr, name);
    loadDictionary(*f.parsed->document.root, d);
    f.loading = false;
    f.dictionary = d;
    return d;
}

void XamlCompiler::loadDictionary(const XamlElement& e, XamlDictionary* d)
{
    const Span here = e.nameSpan;
    if (const auto* source = e.attribute("", "Source"); source && presentation(e, "ResourceDictionary"))
    {
        if (XamlDictionary* file = dictionaryFile(source->value, source->valueSpan))
            d->merged.push_back(file);
    }
    for (const XamlElement* r : objectChildren(e))
    {
        if (r->propertyElement && r->owner == "ResourceDictionary" && r->property == "MergedDictionaries")
        {
            for (const XamlElement* m : objectChildren(*r))
            {
                if (!presentation(*m, "ResourceDictionary"))
                {
                    errorIn(d, m->nameSpan, here, "MergedDictionaries contains ResourceDictionary elements");
                    continue;
                }
                if (const auto* source = m->attribute("", "Source"))
                {
                    if (XamlDictionary* file =
                            dictionaryFile(source->value, d->file.empty() ? source->valueSpan : here))
                        d->merged.push_back(file);
                }
                else
                {
                    XamlDictionary* inlineDictionary = newDictionary(nullptr, d->file);
                    loadDictionary(*m, inlineDictionary);
                    d->merged.push_back(inlineDictionary);
                }
            }
            continue;
        }
        if (r->propertyElement && r->owner == "ResourceDictionary" && r->property == "ThemeDictionaries")
        {
            for (const XamlElement* t : objectChildren(*r))
            {
                const auto* key = t->attribute(kXamlNs, "Key");
                if (!presentation(*t, "ResourceDictionary") || !key)
                {
                    errorIn(d, t->nameSpan, here,
                            "ThemeDictionaries contains ResourceDictionary elements with an x:Key");
                    continue;
                }
                if (key->value != "Light" && key->value != "Dark" && key->value != "Default" &&
                    key->value != "HighContrast")
                {
                    errorIn(d, key->valueSpan, here,
                            "theme dictionary key '" + key->value +
                                "' is not Light, Dark, Default or HighContrast");
                    continue;
                }
                XamlDictionary* theme = newDictionary(d, d->file);
                loadDictionary(*t, theme);
                d->themes[key->value] = theme; // HighContrast is kept but never selected
            }
            continue;
        }
        if (r->propertyElement)
        {
            errorIn(d, r->nameSpan, here,
                    "'" + r->owner + "." + r->property + "' is not supported in a ResourceDictionary");
            continue;
        }
        if (!r->name.condition.empty())
        {
            errorIn(d, r->nameSpan, here, "conditional XAML namespaces are not supported by velorac");
            continue;
        }
        XamlResource res;
        res.element = r;
        res.home = d;
        const std::string type = r->name.ns == kXamlNs ? "x:" + r->name.local : r->name.local;
        const auto* key = r->attribute(kXamlNs, "Key");
        if (type == "Style")
        {
            res.type = XamlResource::Type::kStyle;
            if (!key)
            { // implicit style: keyed by its TargetType
                const auto* target = r->attribute("", "TargetType");
                if (!target)
                {
                    errorIn(d, r->nameSpan, here, "a Style needs a TargetType");
                    continue;
                }
                res.key = target->value;
            }
        }
        else if (type == "ControlTemplate")
        {
            res.type = XamlResource::Type::kTemplate;
        }
        else if (type == "StaticResource")
        {
            res.type = XamlResource::Type::kAlias;
            if (!r->attribute("", "ResourceKey"))
            {
                errorIn(d, r->nameSpan, here, "a StaticResource resource needs a ResourceKey");
                continue;
            }
        }
        else if (type == "SolidColorBrush" || type == "Color")
        {
            res.kind = ValueKind::kBrush;
        }
        else if (type == "x:Double")
        {
            res.kind = ValueKind::kDouble;
        }
        else if (type == "x:Int32")
        {
            res.kind = ValueKind::kInteger;
        }
        else if (type == "x:Boolean")
        {
            res.kind = ValueKind::kBoolean;
        }
        else if (type == "x:String")
        {
            res.kind = ValueKind::kString;
        }
        else if (type == "Thickness")
        {
            res.kind = ValueKind::kThickness;
        }
        else if (type == "CornerRadius")
        {
            res.kind = ValueKind::kCornerRadius;
        }
        else
        {
            errorIn(d, r->nameSpan, here,
                    "'" + (r->name.prefix.empty() ? type : r->name.prefix + ":" + r->name.local) +
                        "' resources are not supported by velorac");
            continue;
        }
        if (res.key.empty())
        {
            if (!key)
            {
                errorIn(d, r->nameSpan, here, "a resource needs an x:Key");
                continue;
            }
            res.key = key->value;
        }
        if (d->entries.contains(res.key))
        {
            errorIn(d, key ? key->span : r->nameSpan, here, "duplicate resource key '" + res.key + "'");
            continue;
        }
        d->entries.emplace(res.key, std::move(res));
    }
}

bool XamlCompiler::elementResources(const XamlElement& e)
{
    const XamlElement* resources = nullptr;
    for (const XamlElement* c : objectChildren(e))
        if (c->propertyElement && c->property == "Resources")
            resources = c;
    if (!resources)
        return false;
    if (resources->owner != e.name.local)
        error(resources->nameSpan,
              "'" + resources->owner + ".Resources' does not belong to '" + e.name.local + "'");
    XamlDictionary* d = newDictionary(m_dictionary);
    const auto children = objectChildren(*resources);
    if (children.size() == 1 && presentation(*children[0], "ResourceDictionary"))
        loadDictionary(*children[0], d);
    else
        loadDictionary(*resources, d);
    m_dictionary = d;
    return true;
}

XamlResource* XamlCompiler::findIn(XamlDictionary* d, std::string_view key, int theme)
{
    if (const auto it = d->entries.find(key); it != d->entries.end())
        return &it->second;
    for (auto m = d->merged.rbegin(); m != d->merged.rend(); ++m)
        if (XamlResource* r = findIn(*m, key, theme))
            return r;
    auto t = d->themes.find(kXamlThemeNames[theme]);
    if (t == d->themes.end())
        t = d->themes.find("Default");
    if (t != d->themes.end())
        if (XamlResource* r = findIn(t->second, key, theme))
            return r;
    return nullptr;
}

XamlResource* XamlCompiler::lookup(std::string_view key, XamlDictionary* d, int theme)
{
    for (; d; d = d->parent)
        if (XamlResource* r = findIn(d, key, theme))
            return r;
    return m_application ? findIn(m_application, key, theme) : nullptr;
}

std::optional<ThemedResourceValue> XamlCompiler::resourceValue(const MarkupExtension& m,
                                                               XamlDictionary* scope, ValueKind wanted,
                                                               std::string_view property)
{
    const auto& keyArgument = resourceKey(m);
    const std::string& key = keyArgument.value.text;
    std::array<XamlResource*, 2> found{lookup(key, scope, 0), lookup(key, scope, 1)};
    if (!found[0] && !found[1])
    {
        error(keyArgument.value.span, "resource '" + key + "' is not defined in this scope");
        return std::nullopt;
    }
    ThemedResourceValue out{wanted};
    for (int theme = 0; theme < 2; ++theme)
    {
        if (!found[theme])
        {
            error(keyArgument.value.span,
                  "resource '" + key + "' is not defined for the " + kXamlThemeNames[theme] + " theme");
            return std::nullopt;
        }
        std::vector<std::string> chain;
        auto value = resolve(*found[theme], theme, m.span, chain);
        if (!value)
            return std::nullopt;
        const ValueKind kind = found[theme]->resolvedKind[theme];
        const bool compatible = kind == wanted || (kind == ValueKind::kInteger && numeric(wanted)) ||
                                (kind == ValueKind::kDouble && wanted == ValueKind::kCornerRadius);
        if (!compatible)
        {
            error(keyArgument.value.span,
                  "resource '" + key + "' is a " + std::string(kindName(kind)) + ", " +
                      std::string(property) + " needs a " + std::string(kindName(wanted)) +
                      (found[0] != found[1] ? std::string(" (") + kXamlThemeNames[theme] + " theme)" : ""));
            return std::nullopt;
        }
        if (kind == ValueKind::kInteger && numeric(wanted))
            value = PropertyValue(static_cast<double>(value->asInt()));
        out.values[theme] = *value;
    }
    return out;
}

std::optional<PropertyValue> XamlCompiler::resolve(XamlResource& r, int theme, const Span& reference,
                                                   std::vector<std::string>& chain)
{
    if (r.state[theme] == XamlResource::State::kDone)
        return r.value[theme];
    if (r.state[theme] == XamlResource::State::kFailed)
        return std::nullopt;
    if (r.state[theme] == XamlResource::State::kResolving)
    {
        std::string cycle;
        for (const auto& k : chain)
            cycle += k + " -> ";
        error(reference, "circular StaticResource reference: " + cycle + r.key);
        return std::nullopt;
    }
    if (r.type == XamlResource::Type::kStyle || r.type == XamlResource::Type::kTemplate)
    {
        error(reference, "resource '" + r.key + "' is a " +
                             (r.type == XamlResource::Type::kStyle ? "Style" : "ControlTemplate") +
                             ", not a value");
        return std::nullopt;
    }
    r.state[theme] = XamlResource::State::kResolving;
    chain.push_back(r.key);
    const XamlElement& e = *r.element;
    std::optional<PropertyValue> value;
    ValueKind kind = r.kind;
    const auto fail = [&](const Span& span, const std::string& message)
    {
        errorIn(r.home, span, reference, message);
        value.reset();
    };
    // Nested references report where they are written when that is in this file.
    const auto inner = [&](const Span& span) { return r.home && !r.home->file.empty() ? reference : span; };
    // A reference from inside this resource resolves in its own dictionary, for the same theme.
    const auto referenced = [&](const MarkupExtension& m) -> XamlResource*
    {
        const auto& key = resourceKey(m);
        XamlResource* other = lookup(key.value.text, r.home, theme);
        if (!other)
            fail(key.value.span, "resource '" + key.value.text + "' is not defined in this scope" +
                                     (lookup(key.value.text, r.home, 1 - theme)
                                          ? std::string(" for the ") + kXamlThemeNames[theme] + " theme"
                                          : ""));
        return other;
    };
    if (r.type == XamlResource::Type::kAlias)
    {
        const auto* key = e.attribute("", "ResourceKey");
        if (XamlResource* other = lookup(key->value, r.home, theme); !other)
        {
            fail(key->valueSpan, "resource '" + key->value + "' is not defined in this scope");
        }
        else if ((value = resolve(*other, theme, inner(key->valueSpan), chain)))
        {
            kind = other->resolvedKind[theme];
        }
    }
    else if (e.name.local == "SolidColorBrush")
    {
        const auto* color = e.attribute("", "Color");
        std::optional<i64> argb;
        if (!color)
        {
            fail(e.nameSpan, "SolidColorBrush needs a Color");
        }
        else if (color->markup &&
                 (color->markup->kind == "StaticResource" || color->markup->kind == "ThemeResource"))
        {
            if (XamlResource* other = referenced(*color->markup))
            {
                if (const auto v = resolve(*other, theme, inner(color->markup->span), chain);
                    v && other->resolvedKind[theme] == ValueKind::kBrush)
                    argb = v->asInt();
                else if (v)
                    fail(resourceKey(*color->markup).value.span,
                         "resource '" + other->key + "' is a " +
                             std::string(kindName(other->resolvedKind[theme])) + ", Color needs a Color");
            }
        }
        else if (color->markup)
        {
            fail(color->markup->span,
                 "SolidColorBrush.Color accepts a color or a StaticResource/ThemeResource");
        }
        else if (const auto c = parseColor(color->value))
        {
            argb = *c;
        }
        else
        {
            fail(color->valueSpan,
                 "'" + color->value + "' is not a color (use #AARRGGBB, #RRGGBB or a named color)");
        }
        if (argb)
        {
            if (const auto* opacity = e.attribute("", "Opacity"))
            {
                if (const auto o = parseNumber(opacity->value); o && *o >= 0 && *o <= 1)
                    argb = (static_cast<i64>(((*argb >> 24) & 0xFF) * *o + 0.5) << 24) | (*argb & 0xFFFFFF);
                else
                    fail(opacity->valueSpan, "Opacity must be a number between 0 and 1");
            }
            if (argb)
                value = PropertyValue(*argb);
        }
        for (const auto& a : e.attributes)
            if (a.kind == XamlAttributeKind::kProperty && a.name.local != "Color" &&
                a.name.local != "Opacity")
                fail(a.span, "SolidColorBrush has no property '" + a.name.local + "' in this subset");
    }
    else
    {
        const std::string text = textOf(e);
        if (r.kind == ValueKind::kString)
        {
            value = PropertyValue(text);
        }
        else
        {
            const SchemaProperty as{"value", r.kind == ValueKind::kBrush ? ValueKind::kBrush : r.kind, "", "",
                                    0};
            auto parsed = parseValue(as, text);
            if (auto* message = std::get_if<std::string>(&parsed))
                fail(e.span, "resource '" + r.key + "': " + *message);
            else
                value = std::get<PropertyValue>(std::move(parsed));
        }
    }
    chain.pop_back();
    r.state[theme] = value ? XamlResource::State::kDone : XamlResource::State::kFailed;
    if (value)
    {
        r.value[theme] = *value;
        r.resolvedKind[theme] = kind;
    }
    return value;
}

std::optional<ThemedResourceValue>
XamlCompiler::literalOrResource(const XamlAttribute& a, XamlDictionary* scope, const SchemaProperty& property)
{
    if (a.markup)
    {
        if (a.markup->kind == "StaticResource" || a.markup->kind == "ThemeResource")
            return resourceValue(*a.markup, scope, property.kind, property.name);
        error(a.markup->span,
              "{" + a.markup->kind +
                  "} is not supported here; use a value, {StaticResource} or {ThemeResource}");
        return std::nullopt;
    }
    auto parsed = parseValue(property, a.value);
    if (auto* message = std::get_if<std::string>(&parsed))
    {
        error(a.valueSpan, *message);
        return std::nullopt;
    }
    ThemedResourceValue v{property.kind};
    v.values[0] = v.values[1] = std::get<PropertyValue>(parsed);
    return v;
}

void XamlCompiler::setValue(std::size_t index, std::string_view property, const ThemedResourceValue& v,
                            bool live, const char* source)
{
    if (v.values[0].empty() && v.values[1].empty())
        return;
    const std::string src = *source ? std::string(", velora::PropertySource::k") + source : "";
    const std::string target = elementExpr(index) + ", p." + memberOf(property);
    if (!v.themed())
        m_scope->build.push_back("    m_ui.set(" + target + ", " + cppValue(v.values[0]) + src + ");\n");
    else
        m_scope->build.push_back("    m_ui." + std::string(live ? "setThemed" : "setForTheme") + "(" +
                                 target + ", velora::Themed{" + cppValue(v.values[0]) + ", " +
                                 cppValue(v.values[1]) + "}" + src + ");\n");
}

// ---- velorac resources ----------------------------------------------------------------

void XamlCompiler::checkDictionary(const XamlDocument& document)
{
    loadMetadataAndPeers();
    const XamlElement& root = *document.root;
    if (!presentation(root, "ResourceDictionary"))
    {
        error(root.nameSpan, "velorac resources checks a ResourceDictionary file");
        return;
    }
    for (const auto& a : root.attributes)
        if (a.kind == XamlAttributeKind::kNamespaceDeclaration)
            m_prefixes[a.name.prefix == "xmlns" ? a.name.local : ""] = a.value;
    m_page.cls = "Check";
    XamlDictionary* d = newDictionary(nullptr);
    m_dictionary = d;
    loadDictionary(root, d);
    // Resolve every entry of this file's dictionaries in both themes.
    std::vector<XamlDictionary*> pending{d};
    while (!pending.empty())
    {
        XamlDictionary* current = pending.back();
        pending.pop_back();
        for (const auto& [name, theme] : current->themes)
            if (name != "HighContrast")
                pending.push_back(theme); // never selected, so never resolved
        for (auto& [key, r] : current->entries)
        {
            if (r.type == XamlResource::Type::kStyle)
            {
                std::vector<std::string> chain;
                const XamlStyle* def = style(r, r.element->nameSpan, chain);
                if (!def)
                    continue;
                const auto* type = findSchemaType(def->target);
                for (const auto& setter : def->setters)
                    if (const auto* value = setter.element->attribute("", "Value"))
                        literalOrResource(*value, setter.home, *findSchemaProperty(setter.property));
                if (def->templateElement && type)
                {
                    m_scope = &m_page;
                    const std::size_t index = m_page.elements++;
                    instantiateTemplate(*r.element, index, *type, *def->templateElement, def->templateHome);
                }
                continue;
            }
            if (r.type == XamlResource::Type::kTemplate)
                continue; // checked where a style or element uses it
            for (int theme = 0; theme < 2; ++theme)
            {
                std::vector<std::string> chain;
                resolve(r, theme, r.element->nameSpan, chain);
            }
        }
    }
    m_result.dependencies.assign(m_dependencies.begin(), m_dependencies.end());
}

} // namespace velora
