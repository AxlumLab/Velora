#include <filesystem>
#include <format>
#include <stdexcept>
#include <velora/controls/ControlTypes.hpp>
#include <velora/controls/ElementSchema.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/controls/XamlItems.hpp>
#include <velora/resources/ResourceProvider.hpp>
#include <velora/runtime/FrameworkResources.hpp>
#include <velora/runtime/object/BuiltinTypes.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/xaml/XamlBinary.hpp>

namespace velora
{

namespace
{
constexpr std::string_view kClassPrefix = "Velora.UI.";

std::string_view shortName(std::string_view className)
{
    return className.starts_with(kClassPrefix) ? className.substr(kClassPrefix.size()) : className;
}

ResourceTemplateFactory templateFactory(std::shared_ptr<const XamlIR> ir, usize begin);
// The code-behind's item accessors and handlers, carried into DataTemplate instances.
struct ItemConnectors
{
    std::function<std::optional<XBindAccessor>(u32, const ObjectRef&)> xbind;
    std::function<std::function<void(const ObjectRef&)>(std::string_view)> connector;
};
ResourceTemplateFactory dataTemplateFactory(std::shared_ptr<const XamlIR> ir, usize begin,
                                            std::shared_ptr<const ItemConnectors> items);

// Builds a ResourceDictionary from a Resources block of compiled XAML (R09.1).
//
// Literal resources and styles go straight in; references ({StaticResource}/{ThemeResource} in a
// resource or a setter) are resolved after the whole block is read, so a ThemeResource may name a
// key defined later, first inside this dictionary (theme dictionary of the referring entry first),
// then in the scope around `element`. A reference cycle is an error. Merged dictionaries are other
// compiled ResourceDictionary files, loaded through the ResourceManager.
class DictionaryBuilder
{
public:
    // `base` is the dictionary file's own URI: relative merged Sources resolve next to it, in its
    // namespace (a framework package's dictionaries merge each other inside the package).
    DictionaryBuilder(Framework& ui, ElementId element, ResourceUri base, std::vector<std::string>& loading,
                      std::shared_ptr<const ItemConnectors> items)
        : m_items(std::move(items)), m_ui(ui), m_element(element), m_base(std::move(base)), m_loading(loading)
    {
    }

    std::string build(const std::shared_ptr<const XamlIR>& shared, usize begin, usize end,
                      ResourceDictionary& out)
    {
        const XamlIR& ir = *shared;
        ResourceDictionary* current = &out;
        i32 theme = -1; // the theme dictionary being read: 0 Light, 1 Dark, 2 Default, -1 none
        for (usize i = begin + 1; i < end; ++i)
        {
            const auto& in = ir.instructions[i];
            const auto at = [&](std::string message)
            { return std::format("{}:{}:{}: {}", ir.sourceName, in.line, in.column, message); };
            switch (in.op)
            {
            case XamlIROp::kResource:
                if (!current->values
                         .emplace(ir.strings[in.a], ResourceValue{{ir.values[in.b], ir.values[in.b]}})
                         .second)
                    return at("duplicate resource key '" + ir.strings[in.a] + "'");
                break;
            case XamlIROp::kResourceReference:
                if (current->values.contains(ir.strings[in.a]))
                    return at("duplicate resource key '" + ir.strings[in.a] + "'");
                current->values[ir.strings[in.a]];
                m_references.push_back(
                    {current, ir.strings[in.a], ir.strings[in.b], in.c != 0, theme, {}, at("")});
                break;
            case XamlIROp::kResourceConverter:
            {
                auto converter = createValueConverter(ir.strings[in.b]);
                if (!converter)
                    return at("no value converter is registered as '" + ir.strings[in.b] + "'");
                if (!current->converters.emplace(ir.strings[in.a], std::move(converter)).second)
                    return at("duplicate resource key '" + ir.strings[in.a] + "'");
                break;
            }
            case XamlIROp::kResourceOpacity:
                for (auto it = m_references.rbegin(); it != m_references.rend(); ++it)
                    if (it->key == ir.strings[in.a] && it->home == current)
                    {
                        it->opacity = ir.values[in.b].asDouble();
                        break;
                    }
                break;
            case XamlIROp::kMergeDictionary:
            {
                auto merged = std::make_shared<ResourceDictionary>();
                if (auto error = merge(ir, ir.strings[in.b], *merged); !error.empty())
                    return at(error);
                current->merged.push_back(std::move(merged));
                break;
            }
            case XamlIROp::kBeginThemeDictionary:
            {
                theme = static_cast<i32>(in.c);
                auto dictionary = std::make_shared<ResourceDictionary>();
                current = dictionary.get();
                if (in.c == static_cast<u32>(XamlThemeDictionary::kDefault))
                    out.defaultTheme = std::move(dictionary);
                else if (in.c == static_cast<u32>(XamlThemeDictionary::kHighContrast))
                    out.highContrastTheme = std::move(dictionary);
                else
                    out.themes[in.c] = std::move(dictionary);
                break;
            }
            case XamlIROp::kEndThemeDictionary:
                current = &out;
                theme = -1;
                break;
            case XamlIROp::kBeginStyle:
            {
                ResourceStyle style;
                style.target = std::string(shortName(ir.types[in.b].className));
                if (in.c != kXamlNoIndex)
                    style.basedOn = ir.strings[in.c];
                const std::string key = in.a == kXamlNoIndex ? style.target : ir.strings[in.a];
                const usize styleEnd = xamlBlockEnd(ir, i);
                for (usize j = i + 1; j < styleEnd; ++j)
                {
                    const auto& setter = ir.instructions[j];
                    if (setter.op == XamlIROp::kSetter)
                        style.setters.push_back({ir.properties[setter.a].name,
                                                 {ir.values[setter.b], ir.values[setter.b]},
                                                 false});
                    else if (setter.op == XamlIROp::kSetterResource)
                    {
                        style.setters.push_back({ir.properties[setter.a].name, {}, setter.c != 0});
                        m_setters.push_back(
                            {current, key, style.setters.size() - 1, ir.strings[setter.b], setter.c != 0,
                             theme, std::format("{}:{}:{}: ", ir.sourceName, setter.line, setter.column)});
                    }
                    else if (setter.op == XamlIROp::kBeginTemplate)
                    {
                        style.templateFactory = templateFactory(shared, j);
                        j = xamlBlockEnd(ir, j);
                    }
                    else if (setter.op == XamlIROp::kStyleTemplateReference)
                        m_templateReferences.push_back(
                            {current, key, ir.strings[setter.b],
                             std::format("{}:{}:{}: ", ir.sourceName, setter.line, setter.column)});
                }
                if (!current->styles.emplace(key, std::move(style)).second)
                    return at("duplicate style key '" + key + "'");
                i = styleEnd;
                break;
            }
            case XamlIROp::kBeginDataTemplate:
                if (!current->dataTemplates.emplace(ir.strings[in.a], dataTemplateFactory(shared, i, m_items))
                         .second)
                    return at("duplicate DataTemplate key '" + ir.strings[in.a] + "'");
                i = xamlBlockEnd(ir, i);
                break;
            case XamlIROp::kBeginTemplate:
            {
                ResourceTemplate entry{std::string(shortName(ir.types[in.b].className)),
                                       templateFactory(shared, i)};
                if (!current->templates.emplace(ir.strings[in.a], std::move(entry)).second)
                    return at("duplicate template key '" + ir.strings[in.a] + "'");
                i = xamlBlockEnd(ir, i);
                break;
            }
            default:
                return at("unexpected instruction in a dictionary");
            }
        }
        return resolveAll(out);
    }

private:
    struct Reference
    {
        ResourceDictionary* home;
        std::string key, target;
        bool theme;
        i32 themeDictionary;
        std::array<PropertyValue, 3> values; // Light, Dark, HighContrast (empty: falls back to Dark)
        std::string where;
        bool resolved = false, resolving = false;
        std::optional<double> opacity; // SolidColorBrush.Opacity over a referenced color
    };
    struct SetterReference
    {
        ResourceDictionary* home;
        std::string style;
        usize setter;
        std::string target;
        bool theme;
        i32 themeDictionary;
        std::string where;
    };

    std::string merge(const XamlIR& ir, const std::string& source, ResourceDictionary& out)
    {
        std::optional<ResourceUri> uri;
        if (source.find("://") != std::string::npos)
            uri = ResourceUri::parse(source);
        else
            uri = ResourceUri::fromPath(
                m_base.nameSpace(),
                (std::filesystem::path(m_base.path()).parent_path() / source).generic_string());
        if (!uri)
            return "'" + source + "' is not a resource path";
        std::string path = uri->path();
        if (path.ends_with(".xaml"))
            path.replace(path.size() - 5, 5, ".vxbf");
        uri = ResourceUri::fromPath(uri->nameSpace(), path);
        if (std::ranges::find(m_loading, uri->toString()) != m_loading.end())
            return "merged dictionary '" + source + "' includes itself";
        const auto bytes = ResourceManager::instance().read(*uri);
        if (!bytes)
            return "merged dictionary '" + source + "' is not compiled into this application (" +
                   uri->toString() + ")";
        auto read = readVxbf(*bytes, schemaFingerprint());
        if (const auto* error = std::get_if<VxbfError>(&read))
            return "merged dictionary '" + source + "': " + error->message;
        auto merged = std::make_shared<const XamlIR>(std::move(std::get<XamlIR>(read)));
        if (merged->instructions.empty() || merged->instructions.front().op != XamlIROp::kBeginResources)
            return "'" + source + "' is not a ResourceDictionary file";
        m_loading.push_back(uri->toString());
        DictionaryBuilder inner(m_ui, m_element, *uri, m_loading, m_items);
        auto error = inner.build(merged, 0, xamlBlockEnd(*merged, 0), out);
        m_loading.pop_back();
        return error;
    }

    // The value of `key` in `theme` (0 Light, 1 Dark, 2 HighContrast): this dictionary's own references and
    // values first (a theme dictionary's entries before the shared ones), then the scope around the element.
    std::optional<PropertyValue> lookup(const ResourceDictionary& root, const std::string& key, usize theme,
                                        std::string& error)
    {
        // The referring theme's own dictionary first, then the shared entries, then Default (which
        // serves the themes without a dictionary of their own).
        const i32 own = theme == 2 ? 3 : static_cast<i32>(theme); // the theme's dictionary kind
        for (const i32 rank : {own, -1, 2})
            for (auto& reference : m_references)
                if (reference.key == key && reference.themeDictionary == rank)
                {
                    if (!resolve(root, reference, error))
                        return std::nullopt;
                    return theme == 2 && reference.values[2].empty() ? reference.values[1]
                                                                     : reference.values[theme];
                }
        std::vector<const ResourceDictionary*> visited;
        if (const auto* value = find(root, key, theme, visited))
            return value->at(theme);
        if (const auto outer = m_ui.resources().findPerTheme(m_element, key))
            return theme == 2 && (*outer)[2].empty() ? (*outer)[1] : (*outer)[theme];
        return std::nullopt;
    }

    // The same order as the runtime's lookup (ResourceScopeStore): own values, merged dictionaries
    // (last first), then the theme dictionary.
    static const ResourceValue* find(const ResourceDictionary& d, const std::string& key, usize theme,
                                     std::vector<const ResourceDictionary*>& visited)
    {
        if (std::ranges::find(visited, &d) != visited.end())
            return nullptr;
        visited.push_back(&d);
        if (const auto it = d.values.find(key); it != d.values.end())
            return &it->second;
        for (auto it = d.merged.rbegin(); it != d.merged.rend(); ++it)
            if (const auto* v = find(**it, key, theme, visited))
                return v;
        const auto selected = theme == 2        ? (d.highContrastTheme ? d.highContrastTheme
                                                   : d.defaultTheme    ? d.defaultTheme
                                                                       : d.themes[1])
                              : d.themes[theme] ? d.themes[theme]
                                                : d.defaultTheme;
        return selected ? find(*selected, key, theme, visited) : nullptr;
    }

    // The themes a reference is evaluated in: a reference inside the Light or Dark dictionary
    // means that theme in both slots; elsewhere a ThemeResource follows the theme and a
    // StaticResource takes the value of the theme current at load.
    std::array<usize, 3> themesOf(bool themeReference, i32 themeDictionary) const
    {
        if (themeDictionary == 0 || themeDictionary == 1)
            return {static_cast<usize>(themeDictionary), static_cast<usize>(themeDictionary),
                    static_cast<usize>(themeDictionary)};
        if (themeDictionary == 3)
            return {2, 2, 2};
        if (themeReference || themeDictionary == 2)
            return {0, 1, 2}; // Default serves whichever themes lack their own dictionary
        const auto now = static_cast<usize>(m_ui.theme());
        return {now, now, now};
    }

    bool resolve(const ResourceDictionary& root, Reference& reference, std::string& error)
    {
        if (reference.resolved)
            return true;
        if (reference.resolving)
        {
            error = reference.where + "resource reference cycle through '" + reference.key + "'";
            return false;
        }
        reference.resolving = true;
        const auto themes = themesOf(reference.theme, reference.themeDictionary);
        for (usize slot = 0; slot < 3; ++slot)
        {
            auto value = lookup(root, reference.target, themes[slot], error);
            if (!value && slot == 2 && error.empty())
                continue; // no high contrast value anywhere: the dark one serves
            if (!value)
            {
                if (error.empty())
                    error = reference.where + "resource '" + reference.target + "' not found";
                return false;
            }
            if (reference.opacity && value->isInt())
            {
                const i64 color = value->asInt();
                const auto alpha =
                    static_cast<i64>((static_cast<u64>(color) >> 24 & 0xFF) * *reference.opacity + 0.5);
                *value = PropertyValue((color & 0x00FFFFFF) | (alpha << 24));
            }
            reference.values[slot] = std::move(*value);
        }
        reference.home->values[reference.key] =
            ResourceValue{{reference.values[0], reference.values[1]}, reference.values[2]};
        reference.resolving = false;
        reference.resolved = true;
        return true;
    }

    // A keyed ControlTemplate in this dictionary (own entries, merged, theme dictionaries), else in
    // the scope around the element.
    static const ResourceTemplate* findTemplate(const ResourceDictionary& d, const std::string& key,
                                                std::vector<const ResourceDictionary*>& visited)
    {
        if (std::ranges::find(visited, &d) != visited.end())
            return nullptr;
        visited.push_back(&d);
        if (const auto it = d.templates.find(key); it != d.templates.end())
            return &it->second;
        for (auto it = d.merged.rbegin(); it != d.merged.rend(); ++it)
            if (const auto* t = findTemplate(**it, key, visited))
                return t;
        for (const auto& themed : {d.themes[0], d.themes[1], d.defaultTheme, d.highContrastTheme})
            if (themed)
                if (const auto* t = findTemplate(*themed, key, visited))
                    return t;
        return nullptr;
    }

    std::string resolveAll(ResourceDictionary& root)
    {
        std::string error;
        for (const auto& t : m_templateReferences)
        {
            std::vector<const ResourceDictionary*> visited;
            const auto* found = findTemplate(root, t.target, visited);
            if (!found)
                return t.where + "ControlTemplate '" + t.target + "' not found";
            auto& style = t.home->styles.at(t.style);
            if (found->target != style.target)
                return t.where + "ControlTemplate '" + t.target + "' targets '" + found->target +
                       "', the style '" + style.target + "'";
            style.templateFactory = found->factory;
        }
        for (auto& reference : m_references)
            if (!resolve(root, reference, error))
                return error;
        for (const auto& s : m_setters)
        {
            auto& setter = s.home->styles.at(s.style).setters[s.setter];
            const auto themes = themesOf(s.theme, s.themeDictionary);
            for (usize slot = 0; slot < 3; ++slot)
            {
                auto value = lookup(root, s.target, themes[slot], error);
                if (!value && slot == 2 && error.empty())
                    continue;
                if (!value)
                    return error.empty() ? s.where + "resource '" + s.target + "' not found" : error;
                (slot == 2 ? setter.highContrast : setter.values[slot]) = std::move(*value);
            }
        }
        return {};
    }

private:
    std::shared_ptr<const ItemConnectors> m_items; // for keyed DataTemplates of this dictionary

    Framework& m_ui;
    ElementId m_element;
    ResourceUri m_base;
    std::vector<std::string>& m_loading;
    std::vector<Reference> m_references;
    std::vector<SetterReference> m_setters;
    struct TemplateReference
    {
        ResourceDictionary* home;
        std::string style, target, where;
    };
    std::vector<TemplateReference> m_templateReferences;
};

// XamlLoader's view of a Framework: handles are ElementIds of that root's tree.
class FrameworkLoadTarget final : public XamlLoadTarget
{
public:
    explicit FrameworkLoadTarget(Framework& ui, const ComponentLoadOptions* options = nullptr) noexcept
        : m_ui(ui), m_options(options)
    {
    }

    u64 schemaFingerprint() const override
    {
        return velora::schemaFingerprint();
    }

    XamlHandle create(const XamlIRType& type, XamlHandle parent, std::string& error) override
    {
        const auto kind = kindOf(type);
        if (!kind)
        {
            error = "not a built-in element type";
            return 0;
        }
        const ElementId id = m_ui.create(*kind, parent);
        if (!id)
            error = parent ? "the parent does not accept it" : "the UI context refused it";
        return id;
    }

    bool isOfType(XamlHandle existing, const XamlIRType& type) const override
    {
        const auto kind = kindOf(type);
        return kind && m_ui.tree().is(existing, *kind);
    }

    std::string setValue(XamlHandle object, const XamlIRProperty& property,
                         const PropertyValue& value) override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return "unknown property (compiled against another schema?)";
        m_ui.set(object, d, value);
        return {};
    }

    std::string setBinding(XamlHandle object, const XamlIRProperty& property, std::string_view path, u32 mode,
                           const XamlBindingOptions& options, XamlHandle source) override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return "unknown property";
        BindingOptions binding;
        binding.converterParameter = options.converterParameter;
        binding.fallback = options.fallback;
        if (!options.converter.empty())
        {
            binding.converter = m_ui.resources().findConverter(object, options.converter);
            if (!binding.converter)
                return "no value converter '" + options.converter + "' in scope";
        }
        if (!source)
        {
            m_ui.bind(object, d, path, static_cast<BindingMode>(mode), std::move(binding));
            return {};
        }
        const PropertyDescriptor* sourceProperty = m_ui.property(path);
        if (!sourceProperty)
            return "the binding source has no property '" + std::string(path) + "'";
        m_ui.bindElement(object, d, source, sourceProperty, static_cast<BindingMode>(mode),
                         std::move(binding));
        return {};
    }

    std::string setResource(XamlHandle object, const XamlIRProperty& property, std::string_view key,
                            bool theme) override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return "unknown property";
        if (theme)
        {
            // {ThemeResource}: the value follows the root's theme (re-resolved on theme change).
            const auto values = m_ui.resources().findPerTheme(object, key);
            if (!values)
                return "resource '" + std::string(key) + "' not found";
            m_ui.setThemed(object, d, Themed{(*values)[0], (*values)[1], (*values)[2]});
            return {};
        }
        // {StaticResource}: resolved once, now.
        const auto value = m_ui.resources().find(object, key);
        if (!value)
            return "resource '" + std::string(key) + "' not found";
        m_ui.set(object, d, *value);
        return {};
    }

    std::string addEvent(XamlHandle object, std::string_view event, std::function<void()> handler) override
    {
        const EventDescriptor* descriptor = nullptr;
        const auto& events = m_ui.events();
#define VELORA_LOOKUP_EVENT(member, name, signature)                                                         \
    if (event == name)                                                                                       \
        descriptor = events.member;
        VELORA_UI_EVENTS(VELORA_LOOKUP_EVENT)
#undef VELORA_LOOKUP_EVENT
        if (!descriptor)
            return "unknown event";
        m_ui.on(object, descriptor, std::move(handler));
        return {};
    }

    void discard(XamlHandle object) noexcept override
    {
        m_ui.tree().remove(object);
    }

    std::optional<PropertyValue> localValue(XamlHandle object, const XamlIRProperty& property) const override
    {
        const PropertyDescriptor* d = descriptor(property);
        const auto element = m_ui.tree().object(object);
        if (!d || !element)
            return std::nullopt;
        return element->readLocalValue(*d);
    }

    void restoreLocal(XamlHandle object, const XamlIRProperty& property,
                      const std::optional<PropertyValue>& value) noexcept override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return;
        try
        {
            if (value)
                m_ui.set(object, d, *value);
            else
                m_ui.clear(object, d);
        }
        catch (...)
        {
        }
    }

    // ---- R09 ---------------------------------------------------------------------------------

    std::string setXBind(XamlHandle object, const XamlIRProperty& property, u32 index, u32 mode) override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return "unknown property";
        auto accessor = m_options && m_options->xbind ? m_options->xbind(index) : std::nullopt;
        if (!accessor)
            return "the code-behind has no accessor for this binding (initialize the page through its "
                   "generated component)";
        m_ui.bindAccessor(object, d, std::move(accessor->get), std::move(accessor->observe),
                          std::move(accessor->set), static_cast<BindingMode>(mode));
        return {};
    }

    std::string setItemTemplate(XamlHandle object, std::string_view key) override
    {
        auto factory = m_ui.resources().findDataTemplate(object, key);
        if (!factory)
            return "no DataTemplate '" + std::string(key) + "' in scope";
        return useItemTemplate(object, std::move(factory));
    }

    std::string setItemTemplate(XamlHandle object, const std::shared_ptr<const XamlIR>& ir,
                                usize begin) override
    {
        return useItemTemplate(object, dataTemplateFactory(ir, begin, itemConnectors()));
    }

    std::string setItemXBind(XamlHandle object, const XamlIRProperty& property, u32 index, u32 mode) override
    {
        const PropertyDescriptor* d = descriptor(property);
        if (!d)
            return "unknown property";
        auto factory = m_options ? m_options->itemXBind : nullptr;
        if (!factory)
            return "the code-behind has no item accessors";
        Framework& ui = m_ui;
        const ElementId element = static_cast<ElementId>(object);
        const auto accessor = [&ui, element, factory, index]() -> std::optional<XBindAccessor>
        {
            const auto& item = ui.tree().property(element, *ui.props().DataContext);
            return item.isObject() && item.asObject() ? factory(index, item.asObject()) : std::nullopt;
        };
        std::function<std::vector<Subscription>(std::function<void()>)> observe;
        if (mode != 0)
            observe = [accessor](std::function<void()> changed)
            {
                const auto a = accessor();
                return a && a->observe ? a->observe(std::move(changed)) : std::vector<Subscription>{};
            };
        std::function<bool(const PropertyValue&)> set;
        if (mode == 2)
            set = [accessor](const PropertyValue& value)
            {
                const auto a = accessor();
                return a && a->set && a->set(value);
            };
        m_ui.bindAccessor(
            element, d,
            [accessor]() -> std::optional<PropertyValue>
            {
                const auto a = accessor();
                return a ? a->get() : std::nullopt;
            },
            std::move(observe), std::move(set), static_cast<BindingMode>(mode), true);
        return {};
    }

    std::string setItemsSource(XamlHandle object, u32 index, u32 mode) override
    {
        if (!m_ui.tree().is(object, ElementKind::kListView))
            return "ItemsSource is supported on a ListView";
        auto accessor = m_options && m_options->xbind ? m_options->xbind(index) : std::nullopt;
        if (!accessor)
            return "the code-behind has no accessor for this binding";
        const auto host = itemsHost(object);
        const auto apply = [weak = std::weak_ptr<XamlItemsHost>(host), get = accessor->get]
        {
            if (const auto h = weak.lock())
                if (const auto value = get())
                    h->setSource(*value);
        };
        apply();
        if (mode != 0 && accessor->observe)
        {
            // A replaced collection object rebinds the host; the subscriptions live with the list.
            auto subscriptions = std::make_shared<std::vector<Subscription>>();
            auto observe = accessor->observe;
            std::function<void()> changed;
            auto self = std::make_shared<std::function<void()>>();
            *self = [apply, observe, subscriptions, weakSelf = std::weak_ptr<std::function<void()>>(self)]
            {
                apply();
                if (const auto again = weakSelf.lock())
                    *subscriptions = observe(*again);
            };
            *subscriptions = observe(*self);
            m_ui.tree().onRemoved(object, [self, subscriptions] { subscriptions->clear(); });
        }
        return {};
    }

    std::string setVisualStates(XamlHandle object, XamlHandle owner, const XamlIR& ir, usize begin, usize end,
                                const std::unordered_map<std::string, XamlHandle>& names) override
    {
        std::vector<VisualStateGroup> groups;
        const auto at = [&](const XamlIRInstruction& in, std::string message)
        { return std::format("{}:{}: {}", in.line, in.column, std::move(message)); };
        // A literal, or a resource's value per theme (StaticResource: the current theme's in both).
        const auto themed = [&](const XamlIRInstruction& in, bool resource, bool theme, u32 index,
                                std::string& error) -> std::optional<Themed>
        {
            if (!resource)
                return Themed{ir.values[index], ir.values[index]};
            const auto& key = ir.strings[index];
            if (theme)
            {
                if (const auto values = m_ui.resources().findPerTheme(object, key))
                    return Themed{(*values)[0], (*values)[1], (*values)[2]};
            }
            else if (const auto value = m_ui.resources().find(object, key))
                return Themed{*value, *value};
            error = at(in, "resource '" + key + "' not found");
            return std::nullopt;
        };
        const auto targetOf = [&](const XamlIRInstruction& in, u32 name, std::string& error) -> ElementId
        {
            const auto it = names.find(ir.strings[name]);
            if (it == names.end())
                error = at(in, "'" + ir.strings[name] + "' is not an x:Name in this scope");
            return it == names.end() ? 0 : static_cast<ElementId>(it->second);
        };
        std::string error;
        for (usize i = begin + 1; i < end && error.empty(); ++i)
        {
            const auto& in = ir.instructions[i];
            switch (in.op)
            {
            case XamlIROp::kBeginStateGroup:
                groups.push_back({ir.strings[in.b], {}});
                break;
            case XamlIROp::kBeginState:
                groups.back().states.push_back({ir.strings[in.b], {}, {}});
                break;
            case XamlIROp::kStateTransition:
                groups.back().transitions.push_back({in.a == kXamlNoIndex ? std::string() : ir.strings[in.a],
                                                     in.b == kXamlNoIndex ? std::string() : ir.strings[in.b],
                                                     in.c / 1000.0, std::nullopt});
                break;
            case XamlIROp::kTransitionEasing:
                groups.back().transitions.back().easing =
                    Easing{static_cast<Easing::Kind>(in.a >> 8), static_cast<Easing::Mode>(in.a & 0xFF)};
                break;
            case XamlIROp::kStateSetter:
            case XamlIROp::kStateSetterResource:
            case XamlIROp::kStateSetterTheme:
            {
                const ElementId target = targetOf(in, in.c, error);
                const auto value = themed(in, in.op != XamlIROp::kStateSetter,
                                          in.op == XamlIROp::kStateSetterTheme, in.b, error);
                if (target && value)
                    groups.back().states.back().setters.push_back(
                        {target, descriptor(ir.properties[in.a]), *value});
                break;
            }
            case XamlIROp::kBeginAnimation:
            {
                AnimationTrack track;
                track.target = targetOf(in, in.b, error);
                track.property = descriptor(ir.properties[in.a]);
                KeyFrame frame;
                const bool keyFrames = in.c == kXamlKeyFrames;
                if (in.c == kXamlDiscrete)
                    frame.interpolation = KeyFrame::Interpolation::kDiscrete;
                else if (in.c != kXamlLinear && !keyFrames)
                {
                    frame.interpolation = KeyFrame::Interpolation::kEased;
                    frame.easing.kind = static_cast<Easing::Kind>(in.c >> 8);
                    frame.easing.mode = static_cast<Easing::Mode>(in.c & 0xFF);
                }
                bool to = false;
                const usize animationEnd = xamlBlockEnd(ir, i);
                for (usize j = i + 1; j < animationEnd && error.empty(); ++j)
                {
                    const auto& v = ir.instructions[j];
                    if (v.op == XamlIROp::kAnimationTiming)
                    {
                        frame.time = v.a / 1000.0;
                        track.begin = v.b / 1000.0;
                        continue;
                    }
                    if (v.op == XamlIROp::kAnimationKeyFrame || v.op == XamlIROp::kAnimationKeyFrameResource)
                    {
                        const bool keyResource = v.op == XamlIROp::kAnimationKeyFrameResource;
                        const auto value =
                            themed(v, keyResource, keyResource && (v.c & kXamlThemeKeyFrame), v.b, error);
                        if (!value)
                            break;
                        KeyFrame key;
                        key.time = v.a / 1000.0;
                        key.value = *value;
                        const u32 interpolation = v.c & ~kXamlThemeKeyFrame;
                        if (interpolation == kXamlKeyFrameDiscrete)
                            key.interpolation = KeyFrame::Interpolation::kDiscrete;
                        else if (interpolation == kXamlKeyFrameLinear)
                            key.interpolation = KeyFrame::Interpolation::kLinear;
                        else
                        {
                            key.interpolation = KeyFrame::Interpolation::kEased;
                            key.easing.kind = static_cast<Easing::Kind>(interpolation >> 8);
                            key.easing.mode = static_cast<Easing::Mode>(interpolation & 0xFF);
                        }
                        track.frames.push_back(std::move(key));
                        to = true;
                        continue;
                    }
                    const bool resource = v.op == XamlIROp::kAnimationResource;
                    const u32 role = resource ? v.a & 1 : v.a;
                    const auto value = themed(v, resource, resource && (v.a & 2), v.b, error);
                    if (!value)
                        break;
                    if (role == static_cast<u32>(XamlAnimationRole::kTo))
                        frame.value = *value, to = true;
                    else
                        track.from = *value;
                }
                if (error.empty() && !to)
                    error = at(in, "an animation needs To");
                if (error.empty() && track.target && track.property)
                {
                    if (!keyFrames)
                        track.frames.push_back(frame);
                    groups.back().states.back().storyboard.push_back(std::move(track));
                }
                i = animationEnd;
                break;
            }
            default:
                break;
            }
        }
        if (!error.empty())
            return error;
        m_ui.setVisualStates(owner, std::move(groups));
        return {};
    }

    std::string applyUid(XamlHandle object, std::string_view uid) override
    {
        const ResourceManager& resources =
            m_options && m_options->resources ? *m_options->resources : ResourceManager::instance();
        const ResourceContext context = m_options ? m_options->resourceContext : ResourceContext{};
        const std::string prefix = "Resources/" + std::string(uid) + ".";
        const auto* type = findSchemaType(m_ui.tree().type(object));
        for (const auto& path : resources.paths(ResourceNamespace::kApplication, prefix))
        {
            const std::string name = path.substr(prefix.size());
            const PropertyDescriptor* d = m_ui.property(name);
            const SchemaProperty* schema = findSchemaProperty(name);
            if (!type || !d || !schema || !hasSchemaProperty(*type, name))
                return "the string resource '" + path + "' names no property of a " +
                       std::string(m_ui.tree().type(object));
            const auto text =
                resources.string(*ResourceUri::fromPath(ResourceNamespace::kApplication, path), context);
            if (!text)
                continue; // not localized for this language: the markup value stays
            auto parsed = parseValue(*schema, *text);
            if (const auto* message = std::get_if<std::string>(&parsed))
                return "'" + path + "': " + *message;
            m_ui.set(object, d, std::move(std::get<PropertyValue>(parsed)));
        }
        return {};
    }

    std::string setResources(XamlHandle object, const std::shared_ptr<const XamlIR>& ir, usize begin,
                             usize end) override
    {
        ResourceDictionary dictionary;
        std::vector<std::string> loading{"ms-appx:///" + ir->sourceName};
        const auto base = ResourceUri::fromPath(ResourceNamespace::kApplication, ir->sourceName);
        DictionaryBuilder builder(m_ui, object, base ? *base : ResourceUri(), loading, itemConnectors());
        if (auto error = builder.build(ir, begin, end, dictionary); !error.empty())
            return error;
        if (!m_ui.resources().setScope(object, std::move(dictionary)))
            return "the object is not in this UI context";
        return {};
    }

    void clearResources(XamlHandle object) noexcept override
    {
        m_ui.resources().clearScope(object);
    }

    std::string applyStyle(XamlHandle object, std::string_view key) override
    {
        return guarded(
            [&]
            {
                return m_ui.resources().applyStyle(object, key)
                           ? std::string()
                           : "no Style '" + std::string(key) + "' for a '" +
                                 std::string(m_ui.tree().type(object)) + "' in scope";
            });
    }

    std::string applyImplicitStyle(XamlHandle object) override
    {
        const std::string type(m_ui.tree().type(object));
        if (!m_ui.resources().hasStyle(object, type))
            return {};
        return applyStyle(object, type);
    }

    std::string applyTemplate(XamlHandle object, std::string_view key) override
    {
        return guarded(
            [&]
            {
                return m_ui.resources().applyTemplate(object, key)
                           ? std::string()
                           : "no ControlTemplate '" + std::string(key) + "' for a '" +
                                 std::string(m_ui.tree().type(object)) + "' in scope";
            });
    }

    std::string applyTemplate(XamlHandle object, const std::shared_ptr<const XamlIR>& ir,
                              usize begin) override
    {
        return guarded(
            [&]
            {
                m_ui.resources().applyTemplate(object, templateFactory(ir, begin));
                return std::string();
            });
    }

    std::string templateBind(XamlHandle object, const XamlIRProperty& property, XamlHandle owner,
                             const XamlIRProperty& ownerProperty) override
    {
        const PropertyDescriptor* d = descriptor(property);
        const PropertyDescriptor* source = descriptor(ownerProperty);
        if (!d || !source)
            return "unknown property";
        m_ui.templateBind(object, d, owner, source);
        return {};
    }

    void completeTemplate(XamlHandle owner, XamlHandle root,
                          const std::unordered_map<std::string, XamlHandle>& names) override
    {
        std::vector<std::pair<std::string, ElementId>> scope;
        for (const auto& [name, handle] : names)
            scope.emplace_back(name, handle);
        m_ui.applyTemplate(owner, root, std::move(scope));
    }

private:
    std::shared_ptr<const ItemConnectors> itemConnectors() const
    {
        if (!m_options || (!m_options->itemXBind && !m_options->itemConnector))
            return nullptr;
        return std::make_shared<const ItemConnectors>(
            ItemConnectors{m_options->itemXBind, m_options->itemConnector});
    }

    std::shared_ptr<XamlItemsHost> itemsHost(ElementId list)
    {
        auto host = XamlItemsHost::find(m_ui, list);
        return host ? host : XamlItemsHost::attach(m_ui, list);
    }

    std::string useItemTemplate(ElementId list, ResourceTemplateFactory factory)
    {
        if (!m_ui.tree().is(list, ElementKind::kListView))
            return "ItemTemplate is supported on a ListView";
        itemsHost(list)->setFactory(
            [this, &ui = m_ui, factory = std::move(factory)](ElementId parent) -> ElementId
            {
                try
                {
                    const auto root = factory(ui, parent);
                    return root ? *std::static_pointer_cast<ElementId>(root) : 0;
                }
                catch (const std::exception&)
                {
                    return 0;
                }
            });
        return {};
    }

    // Template instantiation reports failure by throwing through the runtime's style code (which
    // rolls the instance back); here it becomes the load's error.
    template <class F> static std::string guarded(F&& f)
    {
        try
        {
            return f();
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    }

    static std::optional<ElementKind> kindOf(const XamlIRType& type)
    {
        const std::string_view name = type.className;
        if (!name.starts_with(kClassPrefix))
            return std::nullopt;
        return findElementKind(name.substr(kClassPrefix.size()));
    }

    // The root's descriptor for a compiled property: found by its XAML name, accepted only when its
    // stable id is the one compiled in (a renamed or re-keyed property is refused, not guessed).
    const PropertyDescriptor* descriptor(const XamlIRProperty& property) const
    {
        const PropertyDescriptor* d = m_ui.property(property.name);
        return d && d->id == property.id ? d : nullptr;
    }

    Framework& m_ui;
    const ComponentLoadOptions* m_options;
};

// A DataTemplate's containers: each call replays the template's IR under the list.
ResourceTemplateFactory dataTemplateFactory(std::shared_ptr<const XamlIR> ir, usize begin,
                                            std::shared_ptr<const ItemConnectors> items)
{
    return [ir = std::move(ir), begin, items = std::move(items)](Framework& ui,
                                                                 ElementId parent) -> std::shared_ptr<void>
    {
        ComponentLoadOptions local;
        XamlLoadOptions options;
        options.parent = parent;
        if (items)
        {
            local.itemXBind = items->xbind;
            local.itemConnector = items->connector;
            if (items->connector)
                options.elementConnector = [&ui, items](std::string_view name,
                                                        XamlHandle element) -> std::function<void()>
                {
                    auto handler = items->connector(name);
                    if (!handler)
                        return {};
                    // The container's current item when the event happens (containers are reused).
                    return [&ui, handler = std::move(handler), element = static_cast<ElementId>(element)]
                    {
                        const auto& item = ui.tree().property(element, *ui.props().DataContext);
                        if (item.isObject() && item.asObject())
                            handler(item.asObject());
                    };
                };
        }
        FrameworkLoadTarget target(ui, &local);
        const auto result =
            XamlLoader(controlTypeRegistry()).instantiateDataTemplate(ir, begin, target, options);
        if (!result.ok)
            throw std::runtime_error(result.error);
        // Each instance is its own name scope (x:Name repeats in every container).
        const auto root = static_cast<ElementId>(result.root);
        std::vector<std::pair<std::string, ElementId>> names;
        for (const auto& [name, handle] : result.names)
            names.emplace_back(name, static_cast<ElementId>(handle));
        ui.setNameScope(root, std::move(names));
        return std::make_shared<ElementId>(root);
    };
}

// A ControlTemplate's instances: each call replays the template's IR under the templated control.
ResourceTemplateFactory templateFactory(std::shared_ptr<const XamlIR> ir, usize begin)
{
    return [ir = std::move(ir), begin](Framework& ui, ElementId owner) -> std::shared_ptr<void>
    {
        FrameworkLoadTarget target(ui);
        const auto result = XamlLoader(controlTypeRegistry()).instantiateTemplate(ir, begin, target, owner);
        if (!result.ok)
            throw std::runtime_error(result.error);
        return nullptr; // the instance lives in the tree, registered by completeTemplate
    };
}
} // namespace

const RuntimeTypeRegistry& controlTypeRegistry()
{
    static const RuntimeTypeRegistry& registry = []() -> const RuntimeTypeRegistry&
    {
        static RuntimeTypeRegistry types;
        registerBuiltinTypes(types);
        registerControlTypes(types);
        return types;
    }();
    return registry;
}

ComponentLoad loadComponent(Framework& ui, std::span<const u8> vxbf, const ComponentLoadOptions& options)
{
    ComponentLoad load;
    if (!ui.isOwnerThread())
    {
        load.error = "loadComponent must run on the UI thread";
        return load;
    }
    XamlLoadOptions loadOptions;
    loadOptions.connector = options.connector;
    if (options.existingRoot)
    {
        loadOptions.existingRoot = ui.tree().element(*options.existingRoot);
        if (!loadOptions.existingRoot)
        {
            load.error = "the existing root is not in this UI context";
            return load;
        }
    }
    if (options.parent)
    {
        loadOptions.parent = ui.tree().element(*options.parent);
        if (!loadOptions.parent)
        {
            load.error = "the parent is not in this UI context";
            return load;
        }
    }
    // OnApplyTemplate for this page's elements: registered before loading, because templates are
    // applied while the page loads. The page is the subtree under its root, known once loaded.
    auto pageRoot = std::make_shared<ElementId>(loadOptions.existingRoot);
    u64 templateToken = 0;
    if (options.templateApplied)
        templateToken = ui.addTemplateApplied(
            [&ui, pageRoot, handler = options.templateApplied](ElementId owner)
            {
                for (ElementId e = owner; e; e = ui.tree().parent(e))
                    if (e == *pageRoot)
                    {
                        handler(ui, owner);
                        return;
                    }
                if (*pageRoot == 0) // still loading a created root: everything created is the page's
                    handler(ui, owner);
            });
    FrameworkLoadTarget target(ui, &options);
    XamlLoader loader(controlTypeRegistry());
    const XamlLoadResult result = loader.load(vxbf, target, loadOptions);
    load.ok = result.ok;
    load.error = result.error;
    if (!result.ok)
    {
        ui.removeTemplateApplied(templateToken);
        return load;
    }
    if (templateToken)
    {
        *pageRoot = static_cast<ElementId>(result.root);
        ui.tree().onRemoved(*pageRoot, [&ui, templateToken] { ui.removeTemplateApplied(templateToken); });
    }
    load.root = std::static_pointer_cast<UIElement>(ui.tree().object(result.root));
    for (const auto& [name, handle] : result.names)
        load.names.emplace(name, std::static_pointer_cast<UIElement>(ui.tree().object(handle)));
    return load;
}

ComponentLoad loadComponent(Framework& ui, const ResourceUri& uri, const ComponentLoadOptions& options)
{
    const auto bytes = ResourceManager::instance().read(uri);
    if (!bytes)
    {
        ComponentLoad load;
        load.error = "no compiled XAML at " + uri.toString();
        return load;
    }
    return loadComponent(ui, *bytes, options);
}

} // namespace velora
