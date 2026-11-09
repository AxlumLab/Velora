#include <algorithm>
#include <map>
#include <utility>
#include <velora/controls/ElementSchema.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/xaml/XamlIRBuilder.hpp>

namespace velora
{

namespace
{
constexpr std::string_view kUnsupported = " is not supported by the .vxbf pipeline yet";

bool numeric(ValueKind kind)
{
    return kind == ValueKind::kDouble || kind == ValueKind::kInteger || kind == ValueKind::kCornerRadius;
}

class IRBuilder
{
public:
    IRBuilder(std::string_view sourceName, const MetadataCatalog* catalog) : m_catalog(catalog)
    {
        m_result.ir.sourceName = std::string(sourceName);
        m_result.ir.schemaFingerprint = schemaFingerprint();
    }

    XamlIRResult run(const XamlDocument& document)
    {
        if (!document.root)
            error({}, "the document has no root element");
        else if (presentation(*document.root, "ResourceDictionary"))
        {
            // A ResourceDictionary file: one top-level Resources block, merged by pages that use it.
            emit(XamlIROp::kBeginResources, document.root->nameSpan);
            dictionary(*document.root);
            emit(XamlIROp::kEndResources, document.root->span);
        }
        else
        {
            // x:Class first: the root's own x:Bind paths resolve against it.
            if (const auto* cls = document.root->attribute(kXamlNs, "Class"))
                m_result.ir.className = cls->value;
            for (const auto& a : document.root->attributes)
                if (a.kind == XamlAttributeKind::kNamespaceDeclaration)
                    m_namespaces[a.name.prefix == "xmlns" ? a.name.local : std::string()] = a.value;
            object(*document.root, true);
            checkElementNames();
        }
        return std::move(m_result);
    }

private:
    // ---- helpers ----------------------------------------------------------------------------

    void error(const Span& span, std::string message)
    {
        m_result.diagnostics.push_back({span, std::move(message)});
    }

    void emit(XamlIROp op, const Span& span, u32 a = 0, u32 b = 0, u32 c = 0)
    {
        m_result.ir.instructions.push_back({op, a, b, c, span.begin.line, span.begin.column});
    }

    static bool presentation(const XamlElement& e, std::string_view local)
    {
        return e.name.ns == kXamlPresentationNs && e.name.local == local;
    }

    static std::vector<const XamlElement*> children(const XamlElement& e)
    {
        std::vector<const XamlElement*> out;
        for (const auto& node : e.children)
            if (const auto* child = std::get_if<std::shared_ptr<XamlElement>>(&node);
                child && !(*child)->ignored)
                out.push_back(child->get());
        return out;
    }

    static std::string text(const XamlElement& e)
    {
        std::string out;
        for (const auto& node : e.children)
            if (const auto* t = std::get_if<XamlText>(&node))
                out += t->value;
        const auto first = out.find_first_not_of(" \t\r\n");
        const auto last = out.find_last_not_of(" \t\r\n");
        return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
    }

    static const XamlAttribute* key(const XamlElement& e)
    {
        return e.attribute(kXamlNs, "Key");
    }

    // {StaticResource K} / {ThemeResource K}: the key argument, or null after a diagnostic.
    const MarkupArgument* resourceKey(const MarkupExtension& m)
    {
        if ((m.kind != "StaticResource" && m.kind != "ThemeResource") || !m.positional ||
            m.positional->value.text.empty())
        {
            error(m.span, "expected {StaticResource Key} or {ThemeResource Key}");
            return nullptr;
        }
        return &*m.positional;
    }

    // ---- objects ----------------------------------------------------------------------------

    void object(const XamlElement& element, bool root)
    {
        if (element.name.ns != kXamlPresentationNs)
        {
            error(element.nameSpan, "'" + element.name.local + "': custom and non-presentation types" +
                                        std::string(kUnsupported));
            return;
        }
        const SchemaType* type = findSchemaType(element.name.local);
        if (!type)
        {
            error(element.nameSpan, "unknown element type '" + element.name.local + "'");
            return;
        }
        emit(XamlIROp::kBeginObject, element.nameSpan, m_result.ir.type("Velora.UI." + element.name.local));
        // Resources first: the object's own values and children resolve against them.
        for (const XamlElement* c : children(element))
            if (c->propertyElement && c->property == "Resources")
            {
                if (c->owner != element.name.local)
                    error(c->nameSpan, "'" + c->owner + ".Resources' on a '" + element.name.local + "'");
                else
                    resources(*c);
            }
        std::vector<std::string> assigned;
        for (const auto& attribute : element.attributes)
            this->attribute(*type, attribute, root, assigned);
        content(*type, element, assigned);
        // Inside a ControlTemplate, a ContentPresenter shows the templated parent's content.
        if (type->name == "ContentPresenter" && !m_templates.empty())
            for (const char* implicit : {"Content", "Foreground", "FontSize"})
            {
                const auto* ownerType = findSchemaType(m_templates.back());
                const auto* d = schemaPropertyDescriptor(implicit);
                if (std::ranges::find(assigned, implicit) == assigned.end() && ownerType && d &&
                    hasSchemaProperty(*ownerType, implicit))
                {
                    const u32 property = m_result.ir.property(d->id, implicit);
                    emit(XamlIROp::kSetTemplateBinding, element.nameSpan, property, property);
                }
            }
        if (m_uid != kXamlNoIndex)
            emit(XamlIROp::kSetUid, element.nameSpan, 0, std::exchange(m_uid, kXamlNoIndex));
        emit(XamlIROp::kEndObject, element.span);
    }

    void attribute(const SchemaType& type, const XamlAttribute& a, bool root,
                   std::vector<std::string>& assigned)
    {
        switch (a.kind)
        {
        case XamlAttributeKind::kNamespaceDeclaration:
        case XamlAttributeKind::kIgnored:
        case XamlAttributeKind::kXmlAttribute:
            return;
        case XamlAttributeKind::kDirective:
            if (a.name.local == "Name")
            {
                emit(XamlIROp::kSetName, a.span, 0, m_result.ir.intern(a.value));
                m_scopes.back().names.emplace(a.value, std::string(type.name));
            }
            else if (a.name.local == "Class" && root)
                m_result.ir.className = a.value;
            else if (a.name.local == "Uid")
                m_uid = m_result.ir.intern(a.value); // emitted last, so it overrides markup values
            else
                error(a.span, "x:" + a.name.local + std::string(kUnsupported));
            return;
        case XamlAttributeKind::kProperty:
        case XamlAttributeKind::kAttachedProperty:
            break;
        }
        // Attached properties arrive as "Owner.Name" in the local name already.
        const std::string name = a.owner.empty() || a.name.local.find('.') != std::string::npos
                                     ? a.name.local
                                     : a.owner + "." + a.name.local;
        if (a.owner.empty() && type.name == "ListView" && (name == "ItemsSource" || name == "ItemTemplate"))
        {
            if (name == "ItemTemplate")
            {
                if (const auto* k = a.markup ? resourceKey(*a.markup) : nullptr)
                    emit(XamlIROp::kSetItemTemplate, a.span, 0, m_result.ir.intern(k->value.text));
                else if (!a.markup)
                    error(a.valueSpan, "ItemTemplate takes a {StaticResource} naming a DataTemplate");
            }
            else if (!a.markup || a.markup->kind != "x:Bind")
                error(a.valueSpan, "ItemsSource takes an {x:Bind} to an ObservableVector in this pipeline");
            else
            {
                m_xbindOp = XamlIROp::kSetItemsSource;
                xbind(a, 0);
                m_xbindOp = XamlIROp::kSetXBind;
            }
            return;
        }
        if (a.owner.empty() && (name == "Style" || name == "Template"))
        {
            if (!a.markup)
                error(a.valueSpan, name + " takes a {StaticResource} naming a " +
                                       (name == "Style" ? "Style" : "ControlTemplate"));
            else if (const auto* k = resourceKey(*a.markup))
                emit(name == "Style" ? XamlIROp::kApplyStyle : XamlIROp::kSetTemplate, a.span, 0,
                     m_result.ir.intern(k->value.text));
            return;
        }
        if (a.owner.empty() && hasSchemaEvent(type, name))
        {
            if (a.markup)
                error(a.valueSpan, "an event handler is a method name, not a markup extension");
            else if (!m_templates.empty())
                error(a.span, "event handlers inside a ControlTemplate" + std::string(kUnsupported));
            else if (m_dataTemplates > 0 && m_itemTypes.back().empty())
                error(a.span,
                      "an event handler inside a DataTemplate needs x:DataType (the handler gets the item)");
            else if (m_dataTemplates > 0)
            {
                const auto type = cppType(m_itemTypes.back());
                for (const auto& [handler, itemType] : m_result.itemHandlers)
                    if (handler == a.value && itemType != type)
                    {
                        error(a.span, "a DataTemplate handler cannot be shared by different item types");
                        return;
                    }
                emit(XamlIROp::kAddEvent, a.span, 0, m_result.ir.intern(name), m_result.ir.intern(a.value));
                m_result.itemHandlers.emplace_back(a.value, type);
            }
            else
                emit(XamlIROp::kAddEvent, a.span, 0, m_result.ir.intern(name), m_result.ir.intern(a.value));
            return;
        }
        if (!hasSchemaProperty(type, name))
        {
            error(a.span, "'" + std::string(type.name) + "' has no property or event '" + name + "'");
            return;
        }
        const SchemaProperty* schema = findSchemaProperty(name);
        const PropertyDescriptor* descriptor = schemaPropertyDescriptor(name);
        if (!schema || !descriptor)
        {
            error(a.span, "property '" + name + "'" + std::string(kUnsupported));
            return;
        }
        assigned.push_back(name);
        const u32 property = m_result.ir.property(descriptor->id, name);
        if (a.markup)
        {
            markup(a, type, *schema, property);
            return;
        }
        auto parsed = parseValue(*schema, a.value);
        if (auto* message = std::get_if<std::string>(&parsed))
        {
            error(a.valueSpan, *message);
            return;
        }
        auto& value = std::get<PropertyValue>(parsed);
        if (!value.empty())
            emit(XamlIROp::kSetValue, a.span, property, m_result.ir.value(std::move(value)));
    }

    void markup(const XamlAttribute& a, const SchemaType& type, const SchemaProperty& schema, u32 property)
    {
        const auto& m = *a.markup;
        if (m.kind == "Binding")
        {
            binding(a, schema, property);
            return;
        }
        if (m.kind == "x:Bind")
        {
            xbind(a, property);
            return;
        }
        if (m.kind == "StaticResource" || m.kind == "ThemeResource")
        {
            if (const auto* k = resourceKey(m))
                emit(XamlIROp::kSetResource, a.span, property, m_result.ir.intern(k->value.text),
                     m.kind == "ThemeResource" ? 1 : 0);
            return;
        }
        if (m.kind == "TemplateBinding")
        {
            templateBinding(a, type, schema, property);
            return;
        }
        error(m.span, "{" + m.kind + "}" + std::string(kUnsupported));
    }

    // {Binding} (R09.3): Path and Mode, plus ElementName, RelativeSource Self/TemplatedParent,
    // Converter/ConverterParameter and FallbackValue. Options are emitted before the kSetBinding.
    void binding(const XamlAttribute& a, const SchemaProperty& schema, u32 property)
    {
        const auto& m = *a.markup;
        const auto* pathArgument = m.argument("Path");
        const std::string path = pathArgument   ? pathArgument->value.text
                                 : m.positional ? m.positional->value.text
                                                : "";
        u32 mode = 1; // OneWay, the {Binding} default
        if (const auto* argument = m.argument("Mode"))
        {
            const auto& v = argument->value.text;
            mode = v == "OneTime" ? 0 : v == "OneWay" ? 1 : v == "TwoWay" ? 2 : 3;
            if (mode == 3)
            {
                error(argument->value.span, "unknown binding mode '" + v + "'");
                return;
            }
        }
        bool elementSource = false;
        for (const auto& named : m.named)
        {
            if (named.name == "Path" || named.name == "Mode" || named.name == "ConverterParameter")
                continue;
            if (named.name == "ElementName")
            {
                emit(XamlIROp::kBindElementName, named.nameSpan, 0, m_result.ir.intern(named.value.text));
                m_scopes.back().uses.push_back({named.value.text, path, named.value.span});
                elementSource = true;
            }
            else if (named.name == "RelativeSource")
            {
                const auto* source = named.value.extension.get();
                const MarkupArgument* modeArgument =
                    source ? (source->positional ? &*source->positional : source->argument("Mode")) : nullptr;
                const std::string relative = modeArgument ? modeArgument->value.text : "";
                if (relative != "Self" && relative != "TemplatedParent")
                    error(named.value.span,
                          "RelativeSource is {RelativeSource Self} or {RelativeSource TemplatedParent}");
                else if (relative == "TemplatedParent" && m_templates.empty())
                    error(named.value.span,
                          "RelativeSource TemplatedParent is only valid inside a ControlTemplate");
                else
                    emit(XamlIROp::kBindRelativeSource, named.nameSpan, 0, 0,
                         static_cast<u32>(relative == "Self" ? XamlRelativeSource::kSelf
                                                             : XamlRelativeSource::kTemplatedParent));
                elementSource = true;
            }
            else if (named.name == "Converter")
            {
                const auto* source = named.value.extension.get();
                const MarkupArgument* k =
                    source && source->kind == "StaticResource" ? resourceKey(*source) : nullptr;
                if (!k)
                {
                    error(named.value.span, "Converter takes a {StaticResource} naming a converter resource");
                    continue;
                }
                const auto* parameter = m.argument("ConverterParameter");
                emit(XamlIROp::kBindConverter, named.nameSpan, 0, m_result.ir.intern(k->value.text),
                     parameter ? m_result.ir.intern(parameter->value.text) : kXamlNoIndex);
            }
            else if (named.name == "FallbackValue")
            {
                auto parsed = parseValue(schema, named.value.text);
                if (auto* message = std::get_if<std::string>(&parsed))
                    error(named.value.span, "FallbackValue: " + *message);
                else
                    emit(XamlIROp::kBindFallback, named.nameSpan, 0,
                         m_result.ir.value(std::move(std::get<PropertyValue>(parsed))));
            }
            else
                error(named.nameSpan, "{Binding " + named.name + "}" + std::string(kUnsupported));
        }
        if (m.argument("ConverterParameter") && !m.argument("Converter"))
            error(m.argument("ConverterParameter")->nameSpan, "ConverterParameter without a Converter");
        if (m.argument("ElementName") && m.argument("RelativeSource"))
            error(m.span, "a Binding has one source: ElementName or RelativeSource");
        if (elementSource && (path.empty() || path.find('.') != std::string::npos))
            error(m.span, "a Binding to an element names one of its properties as the Path");
        emit(XamlIROp::kSetBinding, a.span, property, m_result.ir.intern(path), mode);
    }

    // {x:Bind Path, Mode} (R09.4): resolved now against the x:Class's metadata; the code-behind gets
    // the accessor, the .vxbf only its ordinal.
    void xbind(const XamlAttribute& a, u32 property)
    {
        const auto& m = *a.markup;
        if (!m.bind)
            return; // the parser reported the syntax error
        if (!m_templates.empty())
        {
            error(m.span,
                  "x:Bind inside a ControlTemplate" + std::string(kUnsupported) + " (use TemplateBinding)");
            return;
        }
        const bool item = m_dataTemplates > 0;
        if (item && m_itemTypes.back().empty())
        {
            error(m.span, "x:Bind inside a DataTemplate needs x:DataType on the DataTemplate");
            return;
        }
        // Metadata names C++ types ("Demo::MainPage"); the x:Class is the XAML spelling ("Demo.MainPage").
        std::string root = item ? m_itemTypes.back() : m_result.ir.className;
        for (usize i = 0; (i = root.find('.', i)) != std::string::npos; i += 2)
            root.replace(i, 1, "::");
        if (item && (!m_catalog || !m_catalog->find(root)))
        {
            error(m.span, "x:Bind needs the reflection metadata of the item type '" + root + "'");
            return;
        }
        if (!item && (!m_catalog || root.empty() || !m_catalog->find(root)))
        {
            error(m.span, "x:Bind needs the reflection metadata of the x:Class '" + m_result.ir.className +
                              "' (list its header in velora_xaml_load REFLECT)");
            return;
        }
        if (m.bind->call)
        {
            error(m.bind->span, "x:Bind function calls" + std::string(kUnsupported));
            return;
        }
        std::string path;
        for (const auto& step : m.bind->path.steps)
        {
            if (step.kind == BindStep::Kind::kIndex)
            {
                error(step.span, "x:Bind indexers" + std::string(kUnsupported));
                return;
            }
            path += (path.empty() ? "" : ".") + step.name;
        }
        u32 mode = 0;
        for (const auto& named : m.named)
            if (named.name == "Mode")
            {
                const auto& v = named.value.text;
                mode = v == "OneTime" ? 0 : v == "OneWay" ? 1 : v == "TwoWay" ? 2 : 3;
                if (mode == 3)
                {
                    error(named.value.span, "unknown binding mode '" + v + "'");
                    return;
                }
            }
            else if (named.name != "Path")
                error(named.nameSpan, "x:Bind " + named.name + std::string(kUnsupported));
        auto resolved = m_catalog->resolve(root, path, mode == 2);
        if (!resolved.ok)
        {
            error(m.bind->path.span, resolved.error);
            return;
        }
        for (const auto& step : resolved.steps)
            if (step.type.find('*') != std::string::npos)
            {
                error(m.bind->path.span,
                      "x:Bind through the pointer '" + step.member + "'" + std::string(kUnsupported));
                return;
            }
        emit(item ? XamlIROp::kSetItemXBind : m_xbindOp, a.span, property,
             static_cast<u32>(m_result.xbinds.size()), mode);
        m_result.xbinds.push_back({path, std::move(resolved.steps), mode, item ? root : std::string()});
    }

    // ---- visual states (R10.3) ---------------------------------------------------------------------

    // "0:0:0.25" -> milliseconds; nullopt (after a diagnostic) for anything unbounded or malformed.
    std::optional<u32> timespan(const XamlAttribute* a, u32 fallback)
    {
        if (!a)
            return fallback;
        double seconds = 0;
        int fields = 0;
        std::string_view rest = a->value;
        for (;;)
        {
            const auto colon = rest.find(':');
            const auto part = parseNumber(rest.substr(0, colon));
            if (!part || *part < 0)
                break;
            seconds = seconds * 60 + *part;
            ++fields;
            if (colon == std::string_view::npos)
            {
                if (fields == 3 || (fields == 1 && seconds == 0))
                    return static_cast<u32>(seconds * 1000 + 0.5);
                break;
            }
            rest.remove_prefix(colon + 1);
        }
        error(a->valueSpan, "'" + a->value + "' is not a bounded time span (use h:m:s, e.g. 0:0:0.25)");
        return std::nullopt;
    }

    // "(Border.Background).(SolidColorBrush.Color)" -> Background; "(UIElement.Opacity)" -> Opacity.
    static std::string targetProperty(std::string path)
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

    // The property a state setter or animation targets, recorded for the scope check.
    const SchemaProperty* stateTarget(const std::string& name, const std::string& property, const Span& span)
    {
        const SchemaProperty* schema = findSchemaProperty(property);
        if (!schema || !schemaPropertyDescriptor(property))
        {
            error(span, "unknown property '" + property + "'");
            return nullptr;
        }
        m_scopes.back().targets.push_back({name, property, span});
        return schema;
    }

    void visualStates(const XamlElement& p)
    {
        emit(XamlIROp::kBeginVisualStates, p.nameSpan);
        for (const XamlElement* group : children(p))
        {
            if (!presentation(*group, "VisualStateGroup"))
            {
                error(group->nameSpan, "VisualStateGroups contains VisualStateGroup elements");
                continue;
            }
            const auto* groupName = group->attribute(kXamlNs, "Name");
            emit(XamlIROp::kBeginStateGroup, group->nameSpan, 0,
                 m_result.ir.intern(groupName ? groupName->value : ""));
            for (const XamlElement* state : children(*group))
            {
                if (state->propertyElement && state->property == "Transitions")
                {
                    transitions(*state);
                    continue;
                }
                if (!presentation(*state, "VisualState"))
                {
                    error(state->nameSpan, "a VisualStateGroup contains VisualState elements");
                    continue;
                }
                const auto* stateName = state->attribute(kXamlNs, "Name");
                if (!stateName)
                    error(state->nameSpan, "a VisualState needs an x:Name");
                emit(XamlIROp::kBeginState, state->nameSpan, 0,
                     m_result.ir.intern(stateName ? stateName->value : ""));
                for (const XamlElement* c : children(*state))
                {
                    if (c->propertyElement && c->property == "Setters")
                        for (const XamlElement* setter : children(*c))
                            stateSetter(*setter);
                    else if (presentation(*c, "Storyboard") ||
                             (c->propertyElement && c->property == "Storyboard"))
                    {
                        const XamlElement* board = c;
                        if (c->propertyElement)
                        {
                            const auto boards = children(*c);
                            if (boards.size() != 1 || !presentation(*boards[0], "Storyboard"))
                            {
                                error(c->nameSpan, "'VisualState.Storyboard' contains one Storyboard");
                                continue;
                            }
                            board = boards[0];
                        }
                        if (bounded(*board))
                            for (const XamlElement* animation : children(*board))
                                this->animation(*animation);
                    }
                    else
                        error(c->nameSpan,
                              "'" + (c->propertyElement ? c->owner + "." + c->property : c->name.local) +
                                  "' is not supported in a VisualState");
                }
                emit(XamlIROp::kEndState, state->span);
            }
            emit(XamlIROp::kEndStateGroup, group->span);
        }
        emit(XamlIROp::kEndVisualStates, p.span);
    }

    // VisualStateGroup.Transitions: generated transitions (GeneratedDuration, GeneratedEasingFunction).
    // A transition's own Storyboard is reported.
    void transitions(const XamlElement& p)
    {
        for (const XamlElement* t : children(p))
        {
            if (!presentation(*t, "VisualTransition"))
            {
                error(t->nameSpan, "VisualStateGroup.Transitions contains VisualTransition elements");
                continue;
            }
            const auto duration = timespan(t->attribute("", "GeneratedDuration"), 0);
            if (!duration)
                continue;
            std::optional<u32> easing;
            for (const XamlElement* c : children(*t))
            {
                if (c->propertyElement && c->property == "GeneratedEasingFunction")
                {
                    const auto functions = children(*c);
                    static constexpr std::pair<std::string_view, u32> kKinds[] = {
                        {"QuadraticEase", 1}, {"CubicEase", 2},       {"QuarticEase", 3}, {"QuinticEase", 4},
                        {"SineEase", 5},      {"ExponentialEase", 6}, {"CircleEase", 7}};
                    const auto found =
                        functions.size() == 1
                            ? std::ranges::find(kKinds, std::string_view(functions[0]->name.local),
                                                &std::pair<std::string_view, u32>::first)
                            : std::end(kKinds);
                    if (found == std::end(kKinds))
                    {
                        error(c->nameSpan,
                              "GeneratedEasingFunction holds one of the Quadratic..Circle eases");
                        continue;
                    }
                    const auto* m = functions[0]->attribute("", "EasingMode");
                    const u32 mode = !m ? 1 : m->value == "EaseIn" ? 0 : m->value == "EaseInOut" ? 2 : 1;
                    easing = (found->second << 8) | mode;
                }
                else
                    error(c->nameSpan, "a VisualTransition's own Storyboard" + std::string(kUnsupported) +
                                           " (GeneratedDuration transitions are)");
            }
            const auto* from = t->attribute("", "From");
            const auto* to = t->attribute("", "To");
            emit(XamlIROp::kStateTransition, t->nameSpan,
                 from ? m_result.ir.intern(from->value) : kXamlNoIndex,
                 to ? m_result.ir.intern(to->value) : kXamlNoIndex, *duration);
            if (easing)
                emit(XamlIROp::kTransitionEasing, t->nameSpan, *easing);
        }
    }

    bool bounded(const XamlElement& e)
    {
        bool ok = true;
        if (const auto* r = e.attribute("", "RepeatBehavior"); r && r->value != "1x")
            error(r->valueSpan, "RepeatBehavior '" + r->value + "'" + std::string(kUnsupported)), ok = false;
        if (const auto* r = e.attribute("", "AutoReverse"); r && r->value != "False")
            error(r->valueSpan, "AutoReverse" + std::string(kUnsupported)), ok = false;
        return ok;
    }

    void stateSetter(const XamlElement& s)
    {
        const auto* target = s.attribute("", "Target");
        const auto* value = s.attribute("", "Value");
        const auto dot = target ? target->value.rfind('.') : std::string::npos;
        if (!presentation(s, "Setter") || !target || !value || dot == std::string::npos)
        {
            error(s.nameSpan, "a VisualState setter needs Target=\"Name.Property\" and a Value");
            return;
        }
        const std::string name = target->value.substr(0, dot), property = target->value.substr(dot + 1);
        const SchemaProperty* schema = stateTarget(name, property, target->valueSpan);
        if (!schema)
            return;
        const u32 p = m_result.ir.property(schemaPropertyDescriptor(property)->id, property);
        if (value->markup)
        {
            if (const auto* k = resourceKey(*value->markup))
                emit(value->markup->kind == "ThemeResource" ? XamlIROp::kStateSetterTheme
                                                            : XamlIROp::kStateSetterResource,
                     s.nameSpan, p, m_result.ir.intern(k->value.text), m_result.ir.intern(name));
            return;
        }
        auto parsed = parseValue(*schema, value->value);
        if (auto* message = std::get_if<std::string>(&parsed))
            error(value->valueSpan, *message);
        else
            emit(XamlIROp::kStateSetter, s.nameSpan, p,
                 m_result.ir.value(std::move(std::get<PropertyValue>(parsed))), m_result.ir.intern(name));
    }

    void animation(const XamlElement& a)
    {
        const std::string& kind = a.name.local;
        if (kind.ends_with("UsingKeyFrames"))
        {
            keyFrameAnimation(a);
            return;
        }
        if (kind != "DoubleAnimation" && kind != "ColorAnimation")
        {
            error(a.nameSpan,
                  "'" + kind + "'" + std::string(kUnsupported) + " (DoubleAnimation and ColorAnimation are)");
            return;
        }
        if (!bounded(a))
            return;
        const auto* name = a.attribute("", "Storyboard.TargetName");
        const auto* path = a.attribute("", "Storyboard.TargetProperty");
        const auto* to = a.attribute("", "To");
        if (!name || !path || !to)
        {
            error(a.nameSpan, "an animation needs Storyboard.TargetName, Storyboard.TargetProperty and To");
            return;
        }
        if (const auto* by = a.attribute("", "By"))
        {
            error(by->span, "By" + std::string(kUnsupported) + "; use From/To");
            return;
        }
        const std::string property = targetProperty(path->value);
        const SchemaProperty* schema = stateTarget(name->value, property, path->valueSpan);
        if (!schema)
            return;
        if ((kind == "DoubleAnimation" && !numeric(schema->kind)) ||
            (kind == "ColorAnimation" && schema->kind != ValueKind::kBrush))
        {
            error(path->valueSpan,
                  kind + " cannot animate " + property + " (" + std::string(kindName(schema->kind)) + ")");
            return;
        }
        const auto duration = timespan(a.attribute("", "Duration"), 1000);
        const auto begin = timespan(a.attribute("", "BeginTime"), 0);
        if (!duration || !begin)
            return;
        u32 easing = kXamlLinear;
        for (const XamlElement* c : children(a))
        {
            if (!c->propertyElement || c->property != "EasingFunction")
            {
                error(c->nameSpan, "'" + c->name.local + "' is not supported in an animation");
                continue;
            }
            const auto functions = children(*c);
            static constexpr std::pair<std::string_view, u32> kKinds[] = {
                {"QuadraticEase", 1}, {"CubicEase", 2},       {"QuarticEase", 3}, {"QuinticEase", 4},
                {"SineEase", 5},      {"ExponentialEase", 6}, {"CircleEase", 7}};
            const auto found = functions.size() == 1
                                   ? std::ranges::find(kKinds, std::string_view(functions[0]->name.local),
                                                       &std::pair<std::string_view, u32>::first)
                                   : std::end(kKinds);
            if (found == std::end(kKinds))
            {
                error(c->nameSpan, "EasingFunction holds one of the Quadratic..Circle eases");
                continue;
            }
            u32 mode = 1; // EaseOut, the default
            if (const auto* m = functions[0]->attribute("", "EasingMode"))
            {
                mode = m->value == "EaseIn" ? 0 : m->value == "EaseOut" ? 1 : m->value == "EaseInOut" ? 2 : 3;
                if (mode == 3)
                {
                    error(m->valueSpan, "EasingMode is EaseIn, EaseOut or EaseInOut");
                    continue;
                }
            }
            easing = (found->second << 8) | mode;
        }
        const u32 p = m_result.ir.property(schemaPropertyDescriptor(property)->id, property);
        emit(XamlIROp::kBeginAnimation, a.nameSpan, p, m_result.ir.intern(name->value), easing);
        for (const auto& [attribute, role] : {std::pair{a.attribute("", "From"), XamlAnimationRole::kFrom},
                                              std::pair{to, XamlAnimationRole::kTo}})
        {
            if (!attribute)
                continue;
            if (attribute->markup)
            {
                if (const auto* k = resourceKey(*attribute->markup))
                    emit(XamlIROp::kAnimationResource, attribute->span,
                         static_cast<u32>(role) | (attribute->markup->kind == "ThemeResource" ? 2u : 0u),
                         m_result.ir.intern(k->value.text));
                continue;
            }
            auto parsed = parseValue(*schema, attribute->value);
            if (auto* message = std::get_if<std::string>(&parsed))
                error(attribute->valueSpan, *message);
            else
                emit(XamlIROp::kAnimationValue, attribute->span, static_cast<u32>(role),
                     m_result.ir.value(std::move(std::get<PropertyValue>(parsed))));
        }
        emit(XamlIROp::kAnimationTiming, a.nameSpan, *duration, *begin);
        emit(XamlIROp::kEndAnimation, a.span);
    }

    // An XxxAnimationUsingKeyFrames: Discrete, Linear and Easing key frames (Object animations
    // only Discrete), in KeyTime order.
    void keyFrameAnimation(const XamlElement& a)
    {
        if (!bounded(a))
            return;
        const auto* name = a.attribute("", "Storyboard.TargetName");
        const auto* path = a.attribute("", "Storyboard.TargetProperty");
        const std::string prefix =
            a.name.local.substr(0, a.name.local.size() - std::string_view("AnimationUsingKeyFrames").size());
        if (prefix != "Object" && prefix != "Double" && prefix != "Color")
        {
            error(a.nameSpan, "'" + a.name.local + "'" + std::string(kUnsupported));
            return;
        }
        if (!name || !path)
        {
            error(a.nameSpan, "an animation needs Storyboard.TargetName and Storyboard.TargetProperty");
            return;
        }
        const std::string property = targetProperty(path->value);
        const SchemaProperty* schema = stateTarget(name->value, property, path->valueSpan);
        if (!schema)
            return;
        if ((prefix == "Double" && !numeric(schema->kind)) ||
            (prefix == "Color" && schema->kind != ValueKind::kBrush))
        {
            error(path->valueSpan, a.name.local + " cannot animate " + property + " (" +
                                       std::string(kindName(schema->kind)) + ")");
            return;
        }
        const auto begin = timespan(a.attribute("", "BeginTime"), 0);
        if (!begin)
            return;
        const auto frames = children(a);
        if (frames.empty())
        {
            error(a.nameSpan, "a key frame animation needs key frames");
            return;
        }
        const u32 p = m_result.ir.property(schemaPropertyDescriptor(property)->id, property);
        emit(XamlIROp::kBeginAnimation, a.nameSpan, p, m_result.ir.intern(name->value), kXamlKeyFrames);
        u32 last = 0;
        for (const XamlElement* frame : frames)
        {
            const std::string& kind = frame->name.local;
            u32 interpolation = kXamlKeyFrameDiscrete;
            if (kind == "Linear" + prefix + "KeyFrame" && prefix != "Object")
                interpolation = kXamlKeyFrameLinear;
            else if (kind == "Easing" + prefix + "KeyFrame" && prefix != "Object")
            {
                interpolation = (2u << 8) | 1u; // CubicEase EaseOut unless an EasingFunction says otherwise
                if (const auto eased = easingOf(*frame))
                    interpolation = *eased;
                else
                    continue;
            }
            else if (kind != "Discrete" + prefix + "KeyFrame")
            {
                error(frame->nameSpan, "'" + kind + "' is not a key frame of " + a.name.local);
                continue;
            }
            const auto* value = frame->attribute("", "Value");
            const auto keyTime = timespan(frame->attribute("", "KeyTime"), 0);
            if (!value || !keyTime)
            {
                if (!value)
                    error(frame->nameSpan, "a key frame needs a Value");
                continue;
            }
            if (*keyTime < last)
            {
                error(frame->nameSpan, "key frames are in KeyTime order");
                continue;
            }
            last = *keyTime;
            if (value->markup)
            {
                if (const auto* k = resourceKey(*value->markup))
                    emit(XamlIROp::kAnimationKeyFrameResource, frame->nameSpan, *keyTime,
                         m_result.ir.intern(k->value.text),
                         interpolation | (value->markup->kind == "ThemeResource" ? kXamlThemeKeyFrame : 0u));
                continue;
            }
            auto parsed = parseValue(*schema, value->value);
            if (auto* message = std::get_if<std::string>(&parsed))
                error(value->valueSpan, *message);
            else
                emit(XamlIROp::kAnimationKeyFrame, frame->nameSpan, *keyTime,
                     m_result.ir.value(std::move(std::get<PropertyValue>(parsed))), interpolation);
        }
        emit(XamlIROp::kAnimationTiming, a.nameSpan, 0, *begin);
        emit(XamlIROp::kEndAnimation, a.span);
    }

    // An element's <X.EasingFunction>: the packed easing, or nullopt after a diagnostic. Without one,
    // CubicEase EaseOut.
    std::optional<u32> easingOf(const XamlElement& owner)
    {
        u32 easing = (2u << 8) | 1u;
        for (const XamlElement* c : children(owner))
        {
            if (!c->propertyElement || c->property != "EasingFunction")
            {
                error(c->nameSpan, "'" + c->name.local + "' is not supported here");
                return std::nullopt;
            }
            static constexpr std::pair<std::string_view, u32> kKinds[] = {
                {"QuadraticEase", 1}, {"CubicEase", 2},       {"QuarticEase", 3}, {"QuinticEase", 4},
                {"SineEase", 5},      {"ExponentialEase", 6}, {"CircleEase", 7}};
            const auto functions = children(*c);
            const auto found = functions.size() == 1
                                   ? std::ranges::find(kKinds, std::string_view(functions[0]->name.local),
                                                       &std::pair<std::string_view, u32>::first)
                                   : std::end(kKinds);
            if (found == std::end(kKinds))
            {
                error(c->nameSpan, "EasingFunction holds one of the Quadratic..Circle eases");
                return std::nullopt;
            }
            u32 mode = 1;
            if (const auto* m = functions[0]->attribute("", "EasingMode"))
            {
                mode = m->value == "EaseIn" ? 0 : m->value == "EaseOut" ? 1 : m->value == "EaseInOut" ? 2 : 3;
                if (mode == 3)
                {
                    error(m->valueSpan, "EasingMode is EaseIn, EaseOut or EaseInOut");
                    return std::nullopt;
                }
            }
            easing = (found->second << 8) | mode;
        }
        return easing;
    }

    static i64 withOpacity(i64 color, double opacity)
    {
        const auto alpha = static_cast<i64>((static_cast<u64>(color) >> 24 & 0xFF) * opacity + 0.5);
        return (color & 0x00FFFFFF) | (alpha << 24);
    }

    // Every ElementName names an element of its own name scope (the page, or one template), and the
    // bound path is a property of that element's type.
    void checkElementNames()
    {
        for (const auto& t : m_scopes.back().targets)
        {
            const auto it = m_scopes.back().names.find(t.name);
            if (it == m_scopes.back().names.end())
                error(t.span, "'" + t.name + "' is not an x:Name in this name scope");
            else if (const auto* type = findSchemaType(it->second);
                     type && !hasSchemaProperty(*type, t.property))
                error(t.span,
                      "'" + t.name + "' is a " + it->second + ", which has no property '" + t.property + "'");
        }
        const Scope& scope = m_scopes.back();
        for (const auto& use : scope.uses)
        {
            const auto it = scope.names.find(use.name);
            if (it == scope.names.end())
                error(use.span, "ElementName '" + use.name + "' is not an x:Name in this name scope");
            else if (const auto* type = findSchemaType(it->second); type && !use.path.empty() &&
                                                                    use.path.find('.') == std::string::npos &&
                                                                    !hasSchemaProperty(*type, use.path))
                error(use.span,
                      "'" + use.name + "' is a " + it->second + ", which has no property '" + use.path + "'");
        }
    }

    void templateBinding(const XamlAttribute& a, const SchemaType& type, const SchemaProperty& schema,
                         u32 property)
    {
        const auto& m = *a.markup;
        if (m_templates.empty())
        {
            error(m.span, "{TemplateBinding} is only valid inside a ControlTemplate");
            return;
        }
        const MarkupArgument* argument = m.positional ? &*m.positional : m.argument("Property");
        if (!argument || argument->value.text.empty())
        {
            error(m.span, "{TemplateBinding} needs a property name");
            return;
        }
        const std::string& name = argument->value.text;
        const auto* ownerType = findSchemaType(m_templates.back());
        const auto* source = findSchemaProperty(name);
        const auto* descriptor = schemaPropertyDescriptor(name);
        if (!ownerType || !source || !descriptor || !hasSchemaProperty(*ownerType, name))
        {
            error(argument->value.span,
                  "'" + m_templates.back() + "' has no property '" + name + "' to bind in its template");
            return;
        }
        if (source->kind != schema.kind && !(numeric(source->kind) && numeric(schema.kind)))
        {
            error(argument->value.span, std::string(type.name) + "." + std::string(schema.name) + " is a " +
                                            std::string(kindName(schema.kind)) + ", " + m_templates.back() +
                                            "." + name + " is a " + std::string(kindName(source->kind)));
            return;
        }
        emit(XamlIROp::kSetTemplateBinding, a.span, property, m_result.ir.property(descriptor->id, name));
    }

    void content(const SchemaType& type, const XamlElement& element, std::vector<std::string>& assigned)
    {
        std::string text;
        Span textSpan;
        for (const auto& node : element.children)
        {
            if (const auto* t = std::get_if<XamlText>(&node))
            {
                text += t->value;
                if (textSpan.begin.line == 0)
                    textSpan = t->span;
                continue;
            }
            const auto* child = std::get_if<std::shared_ptr<XamlElement>>(&node);
            if (!child)
                continue; // comments
            const XamlElement& c = **child;
            if (c.ignored)
                continue;
            if (c.propertyElement)
            {
                if (c.property != "Resources") // emitted first, see object()
                    propertyElement(type, c);
                continue;
            }
            if (type.content != ContentModel::kChildren && type.content != ContentModel::kSingle)
            {
                error(c.nameSpan, "'" + std::string(type.name) + "' does not take object content" +
                                      (type.content == ContentModel::kText ? " in this pipeline" : ""));
                continue;
            }
            object(c, false);
        }
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
            return;
        if (type.content != ContentModel::kText)
        {
            error(textSpan, "'" + std::string(type.name) + "' does not take text content");
            return;
        }
        const std::string_view name = type.contentProperty;
        const auto* descriptor = schemaPropertyDescriptor(name);
        if (!descriptor)
        {
            error(textSpan, "content property '" + std::string(name) + "'" + std::string(kUnsupported));
            return;
        }
        assigned.emplace_back(name);
        emit(XamlIROp::kSetValue, textSpan, m_result.ir.property(descriptor->id, name),
             m_result.ir.value(PropertyValue(text)));
    }

    void propertyElement(const SchemaType& type, const XamlElement& p)
    {
        if (p.owner == "ListView" && type.name == "ListView" && p.property == "ItemTemplate")
        {
            const auto templates = children(p);
            if (templates.size() != 1 || !presentation(*templates[0], "DataTemplate"))
                error(p.nameSpan, "'ListView.ItemTemplate' contains one DataTemplate");
            else
                dataTemplate(*templates[0], kXamlNoIndex);
            return;
        }
        if (p.owner == type.name && p.property == "Template")
        {
            const auto templates = children(p);
            if (templates.size() != 1 || !presentation(*templates[0], "ControlTemplate"))
            {
                error(p.nameSpan, "'" + p.owner + ".Template' contains one ControlTemplate");
                return;
            }
            controlTemplate(*templates[0], kXamlNoIndex, std::string(type.name));
            return;
        }
        if (p.owner == "VisualStateManager" && p.property == "VisualStateGroups")
        {
            visualStates(p);
            return;
        }
        const bool rows = p.property == "RowDefinitions", columns = p.property == "ColumnDefinitions";
        if (type.name != "Grid" || p.owner != "Grid" || (!rows && !columns))
        {
            error(p.nameSpan,
                  "property element <" + p.owner + "." + p.property + ">" + std::string(kUnsupported));
            return;
        }
        const std::string item = rows ? "RowDefinition" : "ColumnDefinition";
        const std::string size = rows ? "Height" : "Width";
        GridLengths lengths;
        for (const XamlElement* d : children(p))
        {
            if (d->name.local != item)
            {
                error(d->nameSpan, "<Grid." + p.property + "> contains " + item + " elements");
                continue;
            }
            GridLength length;
            if (const auto* a = d->attribute("", size))
            {
                const auto parsed = parseGridLength(a->value);
                if (!parsed)
                {
                    error(a->valueSpan, "'" + a->value + "' is not a GridLength");
                    continue;
                }
                length = *parsed;
            }
            lengths.push_back(length);
        }
        const auto* descriptor = schemaPropertyDescriptor(p.property);
        emit(XamlIROp::kSetValue, p.span, m_result.ir.property(descriptor->id, p.property),
             m_result.ir.value(PropertyValue(std::move(lengths))));
    }

    // ---- resources (R09.1) -------------------------------------------------------------------

    // <Owner.Resources>: one ResourceDictionary element, or the entries directly.
    void resources(const XamlElement& p)
    {
        emit(XamlIROp::kBeginResources, p.nameSpan);
        const auto items = children(p);
        if (items.size() == 1 && presentation(*items[0], "ResourceDictionary") && !key(*items[0]))
            dictionary(*items[0]);
        else
            for (const XamlElement* item : items)
                entry(*item);
        emit(XamlIROp::kEndResources, p.span);
    }

    void dictionary(const XamlElement& d)
    {
        if (const auto* source = d.attribute("", "Source"))
            emit(XamlIROp::kMergeDictionary, source->span, 0, m_result.ir.intern(source->value));
        for (const XamlElement* c : children(d))
        {
            if (c->propertyElement && c->owner == "ResourceDictionary" && c->property == "MergedDictionaries")
            {
                for (const XamlElement* merged : children(*c))
                {
                    const auto* source = merged->attribute("", "Source");
                    if (!presentation(*merged, "ResourceDictionary") || !source || !children(*merged).empty())
                        error(
                            merged->nameSpan,
                            "a merged dictionary is a <ResourceDictionary Source=\"...\"/> in this pipeline");
                    else
                        emit(XamlIROp::kMergeDictionary, source->span, 0, m_result.ir.intern(source->value));
                }
            }
            else if (c->propertyElement && c->owner == "ResourceDictionary" &&
                     c->property == "ThemeDictionaries")
            {
                for (const XamlElement* themed : children(*c))
                    themeDictionary(*themed);
            }
            else if (c->propertyElement)
                error(c->nameSpan, "<" + c->owner + "." + c->property + ">" + std::string(kUnsupported));
            else
                entry(*c);
        }
    }

    void themeDictionary(const XamlElement& d)
    {
        const auto* k = key(d);
        if (!presentation(d, "ResourceDictionary") || !k)
        {
            error(d.nameSpan, "a theme dictionary is a <ResourceDictionary x:Key=\"Light|Dark|Default\">");
            return;
        }
        XamlThemeDictionary theme;
        if (k->value == "Light")
            theme = XamlThemeDictionary::kLight;
        else if (k->value == "Dark")
            theme = XamlThemeDictionary::kDark;
        else if (k->value == "Default")
            theme = XamlThemeDictionary::kDefault;
        else if (k->value == "HighContrast")
            theme = XamlThemeDictionary::kHighContrast;
        else
        {
            error(k->valueSpan, "theme dictionary '" + k->value + "'" + std::string(kUnsupported) +
                                    " (Light, Dark and Default are)");
            return;
        }
        emit(XamlIROp::kBeginThemeDictionary, d.nameSpan, 0, 0, static_cast<u32>(theme));
        for (const XamlElement* c : children(d))
            if (c->propertyElement)
                error(c->nameSpan, "a theme dictionary holds resources only in this pipeline");
            else
                entry(*c);
        emit(XamlIROp::kEndThemeDictionary, d.span);
    }

    void entry(const XamlElement& r)
    {
        const std::string type = r.name.ns == kXamlNs ? "x:" + r.name.local : r.name.local;
        if (type == "Style")
        {
            style(r);
            return;
        }
        if (type == "DataTemplate")
        {
            if (const auto* k = key(r))
                dataTemplate(r, m_result.ir.intern(k->value));
            else
                error(r.nameSpan, "a DataTemplate resource needs an x:Key");
            return;
        }
        if (r.name.ns.starts_with("using:"))
        {
            // A value converter resource: an instance of a class the application registered
            // (registerValueConverter); only converters may be declared as custom resources here.
            const auto* k = key(r);
            if (!k)
                error(r.nameSpan, "a resource needs an x:Key");
            else if (!r.attributes.empty() &&
                     std::ranges::any_of(r.attributes, [](const XamlAttribute& x)
                                         { return x.kind == XamlAttributeKind::kProperty; }))
                error(r.nameSpan, "converter resources take no properties in this pipeline");
            else
                emit(XamlIROp::kResourceConverter, r.nameSpan, m_result.ir.intern(k->value),
                     m_result.ir.intern(r.name.ns.substr(6) + "." + r.name.local));
            return;
        }
        const auto* k = key(r);
        if (type == "ControlTemplate")
        {
            const auto* target = r.attribute("", "TargetType");
            if (!k)
                error(r.nameSpan, "a ControlTemplate resource needs an x:Key");
            else if (!target)
                error(r.nameSpan, "a ControlTemplate needs a TargetType");
            else
                controlTemplate(r, m_result.ir.intern(k->value), target->value);
            return;
        }
        if (!k)
        {
            error(r.nameSpan, "a resource needs an x:Key");
            return;
        }
        const u32 name = m_result.ir.intern(k->value);
        if (type == "StaticResource")
        {
            if (const auto* target = r.attribute("", "ResourceKey"))
                emit(XamlIROp::kResourceReference, r.nameSpan, name, m_result.ir.intern(target->value), 0);
            else
                error(r.nameSpan, "a StaticResource resource needs a ResourceKey");
            return;
        }
        if (type == "SolidColorBrush")
        {
            for (const auto& a : r.attributes)
                if (a.kind == XamlAttributeKind::kProperty && a.name.local != "Color" &&
                    a.name.local != "Opacity")
                    error(a.span, "SolidColorBrush." + a.name.local + std::string(kUnsupported));
            const auto* color = r.attribute("", "Color");
            std::optional<double> opacity;
            if (const auto* o = r.attribute("", "Opacity"))
            {
                opacity = parseNumber(o->value);
                if (!opacity || *opacity < 0 || *opacity > 1)
                {
                    error(o->valueSpan, "Opacity is a number from 0 to 1");
                    return;
                }
            }
            if (!color)
                error(r.nameSpan, "a SolidColorBrush resource needs a Color");
            else if (color->markup)
            {
                if (const auto* target = resourceKey(*color->markup))
                {
                    emit(XamlIROp::kResourceReference, r.nameSpan, name,
                         m_result.ir.intern(target->value.text),
                         color->markup->kind == "ThemeResource" ? 1 : 0);
                    if (opacity)
                        emit(XamlIROp::kResourceOpacity, r.nameSpan, name,
                             m_result.ir.value(PropertyValue(*opacity)));
                }
            }
            else if (const auto parsed = parseColor(color->value))
                emit(XamlIROp::kResource, r.nameSpan, name,
                     m_result.ir.value(PropertyValue(opacity ? withOpacity(*parsed, *opacity) : *parsed)));
            else
                error(color->valueSpan, "'" + color->value + "' is not a color");
            return;
        }
        const std::string value = text(r);
        std::optional<PropertyValue> parsed;
        if (type == "Color")
        {
            if (const auto c = parseColor(value))
                parsed = PropertyValue(*c);
        }
        else if (type == "x:Double")
        {
            if (const auto d = parseNumber(value))
                parsed = PropertyValue(*d);
        }
        else if (type == "CornerRadius")
        {
            // The runtime keeps one radius: "4" or a uniform "4,4,4,4".
            std::optional<double> uniform;
            bool ok = true;
            for (usize start = 0; ok && start <= value.size();)
            {
                const usize comma = std::min(value.find(',', start), value.size());
                const auto d = parseNumber(value.substr(start, comma - start));
                ok = d && (!uniform || *uniform == *d);
                uniform = d;
                start = comma + 1;
            }
            if (ok && uniform)
                parsed = PropertyValue(*uniform);
        }
        else if (type == "x:Int32")
        {
            if (const auto d = parseNumber(value); d && *d == static_cast<double>(static_cast<i64>(*d)))
                parsed = PropertyValue(static_cast<i64>(*d));
        }
        else if (type == "x:Boolean")
        {
            if (value == "True" || value == "true" || value == "False" || value == "false")
                parsed = PropertyValue(value == "True" || value == "true");
        }
        else if (type == "x:String")
            parsed = PropertyValue(value);
        else if (type == "Thickness")
        {
            if (const auto t = parseThickness(value))
                parsed = PropertyValue(*t);
        }
        else
        {
            error(r.nameSpan, "'" + type + "' resources" + std::string(kUnsupported));
            return;
        }
        if (!parsed)
            error(r.span, "'" + value + "' is not a valid " + type);
        else
            emit(XamlIROp::kResource, r.nameSpan, name, m_result.ir.value(std::move(*parsed)));
    }

    void style(const XamlElement& s)
    {
        const auto* target = s.attribute("", "TargetType");
        const SchemaType* type = target ? findSchemaType(target->value) : nullptr;
        if (!type)
        {
            error(target ? target->valueSpan : s.nameSpan,
                  target ? "unknown TargetType '" + target->value + "'" : "a Style needs a TargetType");
            return;
        }
        u32 basedOn = kXamlNoIndex;
        if (const auto* b = s.attribute("", "BasedOn"))
        {
            const MarkupArgument* base =
                b->markup && b->markup->kind == "StaticResource" ? resourceKey(*b->markup) : nullptr;
            if (!base)
            {
                error(b->valueSpan, "BasedOn takes a {StaticResource} naming a Style");
                return;
            }
            basedOn = m_result.ir.intern(base->value.text);
        }
        const auto* k = key(s);
        emit(XamlIROp::kBeginStyle, s.nameSpan, k ? m_result.ir.intern(k->value) : kXamlNoIndex,
             m_result.ir.type("Velora.UI." + target->value), basedOn);
        for (const XamlElement* c : children(s))
            if (c->propertyElement && c->owner == "Style" && c->property == "Setters")
                for (const XamlElement* setter : children(*c))
                    this->setter(*type, *setter);
            else
                setter(*type, *c);
        emit(XamlIROp::kEndStyle, s.span);
    }

    void setter(const SchemaType& type, const XamlElement& s)
    {
        if (!presentation(s, "Setter"))
        {
            error(s.nameSpan, "a Style holds Setters");
            return;
        }
        const auto* name = s.attribute("", "Property");
        if (!name)
        {
            error(s.nameSpan, "a Setter needs a Property (Setter.Target belongs to visual states, R10)");
            return;
        }
        const XamlElement* valueElement = nullptr;
        for (const XamlElement* c : children(s))
            if (c->propertyElement && c->owner == "Setter" && c->property == "Value")
            {
                const auto values = children(*c);
                if (values.size() == 1)
                    valueElement = values[0];
                else
                    error(c->nameSpan, "Setter.Value holds one object");
            }
        if (name->value == "Template")
        {
            const auto* reference = s.attribute("", "Value");
            if (!valueElement && reference && reference->markup &&
                reference->markup->kind == "StaticResource")
            {
                if (const auto* k = resourceKey(*reference->markup))
                    emit(XamlIROp::kStyleTemplateReference, s.nameSpan, 0, m_result.ir.intern(k->value.text));
            }
            else if (!valueElement || !presentation(*valueElement, "ControlTemplate"))
                error(s.nameSpan, "a Template setter's value is a <ControlTemplate> in <Setter.Value>");
            else
                controlTemplate(*valueElement, kXamlNoIndex, std::string(type.name));
            return;
        }
        if (!hasSchemaProperty(type, name->value))
        {
            error(name->valueSpan, "'" + std::string(type.name) + "' has no property '" + name->value + "'");
            return;
        }
        const SchemaProperty* schema = findSchemaProperty(name->value);
        const PropertyDescriptor* descriptor = schemaPropertyDescriptor(name->value);
        if (!schema || !descriptor)
        {
            error(name->valueSpan, "property '" + name->value + "'" + std::string(kUnsupported));
            return;
        }
        const u32 property = m_result.ir.property(descriptor->id, name->value);
        if (valueElement)
        {
            const auto* color = valueElement->attribute("", "Color");
            if (!presentation(*valueElement, "SolidColorBrush") || schema->kind != ValueKind::kBrush ||
                !color)
                error(valueElement->nameSpan, "this Setter.Value object" + std::string(kUnsupported));
            else if (color->markup)
            {
                if (const auto* k = resourceKey(*color->markup))
                    emit(XamlIROp::kSetterResource, s.nameSpan, property, m_result.ir.intern(k->value.text),
                         color->markup->kind == "ThemeResource" ? 1 : 0);
            }
            else if (const auto parsed = parseColor(color->value))
            {
                const auto* o = valueElement->attribute("", "Opacity");
                const auto opacity = o ? parseNumber(o->value) : std::optional<double>(1.0);
                if (!opacity)
                    error(o->valueSpan, "Opacity is a number from 0 to 1");
                else
                    emit(XamlIROp::kSetter, s.nameSpan, property,
                         m_result.ir.value(PropertyValue(withOpacity(*parsed, *opacity))));
            }
            else
                error(color->valueSpan, "'" + color->value + "' is not a color");
            return;
        }
        const auto* value = s.attribute("", "Value");
        if (!value)
        {
            error(s.nameSpan, "Setter '" + name->value + "' needs a Value");
            return;
        }
        if (value->markup)
        {
            if (const auto* k = resourceKey(*value->markup))
                emit(XamlIROp::kSetterResource, s.nameSpan, property, m_result.ir.intern(k->value.text),
                     value->markup->kind == "ThemeResource" ? 1 : 0);
            return;
        }
        auto parsed = parseValue(*schema, value->value);
        if (auto* message = std::get_if<std::string>(&parsed))
            error(value->valueSpan, *message);
        else
            emit(XamlIROp::kSetter, s.nameSpan, property,
                 m_result.ir.value(std::move(std::get<PropertyValue>(parsed))));
    }

    // ---- templates (R09.5) --------------------------------------------------------------------

    // A DataTemplate: one root object, its own name scope; {Binding} inside resolves against the
    // item (the container's DataContext). x:DataType is required for x:Bind and item handlers.
    void dataTemplate(const XamlElement& t, u32 key)
    {
        const auto roots = children(t);
        if (roots.size() != 1 || roots[0]->propertyElement)
        {
            error(t.nameSpan, "a DataTemplate has exactly one root element");
            return;
        }
        emit(XamlIROp::kBeginDataTemplate, t.nameSpan, key);
        // x:DataType="local:Person" with xmlns:local="using:App" is the C++ type App::Person.
        std::string itemType;
        if (const auto* dataType = t.attribute(kXamlNs, "DataType"))
        {
            const auto colon = dataType->value.find(':');
            const std::string prefix = colon == std::string::npos ? "" : dataType->value.substr(0, colon);
            const auto ns = m_namespaces.find(prefix);
            if (ns == m_namespaces.end() || !ns->second.starts_with("using:"))
                error(dataType->valueSpan,
                      "x:DataType names a type in a using: namespace declared on the page");
            else
                itemType = cppType(ns->second.substr(6) + "." + dataType->value.substr(colon + 1));
        }
        m_itemTypes.push_back(itemType);
        ++m_dataTemplates;
        m_scopes.emplace_back();
        object(*roots[0], false);
        checkElementNames();
        m_scopes.pop_back();
        --m_dataTemplates;
        m_itemTypes.pop_back();
        emit(XamlIROp::kEndDataTemplate, t.span);
    }

    void controlTemplate(const XamlElement& t, u32 key, const std::string& target)
    {
        if (const auto* declared = t.attribute("", "TargetType"); declared && declared->value != target)
        {
            error(declared->valueSpan,
                  "the ControlTemplate targets '" + declared->value + "', not '" + target + "'");
            return;
        }
        if (!findSchemaType(target))
        {
            error(t.nameSpan, "unknown TargetType '" + target + "'");
            return;
        }
        const auto roots = children(t);
        if (roots.size() != 1 || roots[0]->propertyElement)
        {
            error(t.nameSpan, "a ControlTemplate has exactly one root element");
            return;
        }
        emit(XamlIROp::kBeginTemplate, t.nameSpan, key, m_result.ir.type("Velora.UI." + target));
        m_templates.push_back(target);
        m_scopes.emplace_back(); // a template instance has its own name scope
        object(*roots[0], false);
        checkElementNames();
        m_scopes.pop_back();
        m_templates.pop_back();
        emit(XamlIROp::kEndTemplate, t.span);
    }

    struct Scope
    {
        std::map<std::string, std::string> names; // x:Name -> element type
        struct Use
        {
            std::string name, path;
            Span span;
        };
        std::vector<Use> uses; // ElementName references
        struct Target
        {
            std::string name, property;
            Span span;
        };
        std::vector<Target> targets; // visual state setter and animation targets
    };

    const MetadataCatalog* m_catalog = nullptr;
    XamlIROp m_xbindOp = XamlIROp::kSetXBind;        // kSetItemsSource while compiling ListView.ItemsSource
    int m_dataTemplates = 0;                         // DataTemplates being compiled
    std::vector<std::string> m_itemTypes;            // their x:DataType (C++), empty when absent
    std::map<std::string, std::string> m_namespaces; // the page's xmlns prefixes

    static std::string cppType(std::string name)
    {
        for (usize i = 0; (i = name.find('.', i)) != std::string::npos; i += 2)
            name.replace(i, 1, "::");
        return name;
    }
    u32 m_uid = kXamlNoIndex; // x:Uid of the object whose attributes are being read
    XamlIRResult m_result;
    std::vector<std::string> m_templates; // target types of the ControlTemplates being compiled
    std::vector<Scope> m_scopes{1};       // name scopes: the page, then each template being compiled
};
} // namespace

XamlIRResult buildXamlIR(const XamlDocument& document, std::string_view sourceName,
                         const MetadataCatalog* catalog)
{
    return IRBuilder(sourceName, catalog).run(document);
}

} // namespace velora
