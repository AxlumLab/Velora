// velorac: styles (BasedOn, implicit), ControlTemplate instances and {TemplateBinding}.
#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <format>

namespace velora
{

// ---- Styles ---------------------------------------------------------------------------

const XamlStyle* XamlCompiler::style(XamlResource& r, const Span& reference, std::vector<std::string>& chain)
{
    if (const auto it = m_styles.find(&r); it != m_styles.end())
        return it->second ? &*it->second : nullptr;
    if (std::ranges::find(chain, r.key) != chain.end())
    {
        std::string cycle;
        for (const auto& k : chain)
            cycle += k + " -> ";
        errorIn(r.home, r.element->nameSpan, reference, "circular Style BasedOn: " + cycle + r.key);
        return nullptr;
    }
    chain.push_back(r.key);
    auto def = inlineStyle(*r.element, r.home, reference, chain);
    chain.pop_back();
    auto& slot = m_styles[&r];
    slot = std::move(def);
    return slot ? &*slot : nullptr;
}

std::optional<XamlStyle> XamlCompiler::inlineStyle(const XamlElement& e, XamlDictionary* home,
                                                   const Span& reference, std::vector<std::string>& chain)
{
    const auto* target = e.attribute("", "TargetType");
    if (!target)
    {
        errorIn(home, e.nameSpan, reference, "a Style needs a TargetType");
        return std::nullopt;
    }
    const auto* type = findSchemaType(target->value);
    if (!type)
    {
        errorIn(home, target->valueSpan, reference,
                "Style TargetType '" + target->value + "' is not a Velora element type");
        return std::nullopt;
    }
    XamlStyle def;
    if (const auto* basedOn = e.attribute("", "BasedOn"))
    {
        if (!basedOn->markup || basedOn->markup->kind != "StaticResource")
        {
            errorIn(home, basedOn->valueSpan, reference, "BasedOn takes a {StaticResource} naming a Style");
            return std::nullopt;
        }
        const auto& key = resourceKey(*basedOn->markup);
        XamlResource* base = lookup(key.value.text, home, 0);
        if (!base || base->type != XamlResource::Type::kStyle)
        {
            errorIn(home, key.value.span, reference,
                    "BasedOn '" + key.value.text + "' is not a Style in this scope");
            return std::nullopt;
        }
        const XamlStyle* baseDef = style(*base, reference, chain);
        if (!baseDef)
            return std::nullopt;
        if (baseDef->target != type->name)
        {
            errorIn(home, key.value.span, reference,
                    "BasedOn style targets '" + baseDef->target + "', this style targets '" +
                        std::string(type->name) + "'");
            return std::nullopt;
        }
        def = *baseDef;
    }
    def.target = type->name;
    for (const auto& a : e.attributes)
        if (a.kind == XamlAttributeKind::kProperty && a.name.local != "TargetType" &&
            a.name.local != "BasedOn")
            errorIn(home, a.span, reference, "Style has no attribute '" + a.name.local + "' in this subset");
    for (const XamlElement* s : objectChildren(e))
    {
        if (!s->name.condition.empty())
        {
            errorIn(home, s->nameSpan, reference, "conditional XAML namespaces are not supported by velorac");
            continue;
        }
        if (!presentation(*s, "Setter"))
        {
            errorIn(home, s->nameSpan, reference, "a Style contains Setter elements");
            continue;
        }
        const auto* property = s->attribute("", "Property");
        if (!property)
        {
            errorIn(home, s->nameSpan, reference, "a Setter needs a Property");
            continue;
        }
        if (property->value == "Template")
        {
            const XamlElement* controlTemplate = nullptr;
            XamlDictionary* templateHome = home;
            if (const auto* value = s->attribute("", "Value");
                value && value->markup && value->markup->kind == "StaticResource")
            {
                const auto& key = resourceKey(*value->markup);
                XamlResource* r = lookup(key.value.text, home, 0);
                if (r && r->type == XamlResource::Type::kTemplate)
                    controlTemplate = r->element, templateHome = r->home;
                else
                    errorIn(home, key.value.span, reference,
                            "resource '" + key.value.text + "' is not a ControlTemplate");
            }
            else
            {
                for (const XamlElement* v : objectChildren(*s))
                    if (v->propertyElement && v->property == "Value")
                        for (const XamlElement* t : objectChildren(*v))
                            if (presentation(*t, "ControlTemplate"))
                                controlTemplate = t;
                if (!controlTemplate)
                    errorIn(home, s->nameSpan, reference, "a Template setter needs a ControlTemplate value");
            }
            if (controlTemplate)
            {
                def.templateElement = controlTemplate;
                def.templateHome = templateHome;
            }
            continue;
        }
        if (!findSchemaProperty(property->value) || !hasSchemaProperty(*type, property->value))
        {
            errorIn(home, property->valueSpan, reference,
                    "Setter: '" + std::string(type->name) + "' has no property '" + property->value +
                        "' in the Velora runtime");
            continue;
        }
        std::erase_if(def.setters, [&](const XamlStyle::Setter& existing)
                      { return existing.property == property->value; });
        def.setters.push_back({property->value, s, home});
    }
    return def;
}

void XamlCompiler::publishResources(const XamlElement& e, std::size_t index)
{
    // Publish the same dictionaries and resolved values used by compiler lowering into
    // native scopes. This executes after the element is created; bindings remain unchanged.
    const auto publish = [&](XamlDictionary* dictionary, bool application)
    {
        if (!dictionary)
            return;
        std::size_t next = 0;
        std::function<std::string(XamlDictionary*)> emitDictionary;
        m_scope->build.push_back("    {\n");
        emitDictionary = [&](XamlDictionary* current) -> std::string
        {
            const std::string variable = "resources" + std::to_string(next++);
            m_scope->build.push_back("    auto " + variable +
                                     " = std::make_shared<velora::ResourceDictionary>();\n");
            for (auto& [key, resource] : current->entries)
            {
                if (resource.type == XamlResource::Type::kTemplate)
                    continue;
                if (resource.type == XamlResource::Type::kStyle)
                {
                    std::vector<std::string> styleChain;
                    const auto* definition = style(resource, resource.element->nameSpan, styleChain);
                    if (!definition)
                        continue;
                    const std::string destination = variable + "->styles[" + cppString(key) + "]";
                    m_scope->build.push_back("    " + destination +
                                             ".target = " + cppString(definition->target) + ";\n");
                    if (definition->templateElement)
                    {
                        // Reuse the existing generated template class without capturing a page/controller.
                        const auto originalBuildSize = m_scope->build.size();
                        const auto originalMembersSize = m_scope->controlTemplates.size();
                        instantiateTemplate(*resource.element, index, *findSchemaType(definition->target),
                                            *definition->templateElement, definition->templateHome);
                        m_scope->build.resize(originalBuildSize);
                        m_scope->controlTemplates.resize(originalMembersSize);
                        if (const auto it = m_controlTemplateClasses.find(definition->templateElement);
                            it != m_controlTemplateClasses.end())
                        {
                            const std::string qualified =
                                "::" + (m_class.ns.empty() ? "" : m_class.ns + "::") + m_page.cls +
                                "::" + it->second;
                            m_scope->build.push_back("    " + destination +
                                                     ".templateFactory = [](velora::Framework& ui, "
                                                     "velora::ElementId owner) -> std::shared_ptr<void> { "
                                                     "return std::make_shared<" +
                                                     qualified + ">(ui, owner); };\n");
                        }
                        else
                            m_scope->build.push_back("    " + destination + ".supported = false;\n");
                    }
                    for (const auto& setter : definition->setters)
                    {
                        const auto* attribute = setter.element->attribute("", "Value");
                        std::optional<ThemedResourceValue> value;
                        bool live = false;
                        if (attribute)
                        {
                            value = literalOrResource(*attribute, setter.home,
                                                      *findSchemaProperty(setter.property));
                            live = attribute->markup && attribute->markup->kind == "ThemeResource";
                        }
                        else
                        {
                            for (const auto* propertyElement : objectChildren(*setter.element))
                                if (propertyElement->propertyElement && propertyElement->property == "Value")
                                    for (const auto* brush : objectChildren(*propertyElement))
                                        if (presentation(*brush, "SolidColorBrush") &&
                                            findSchemaProperty(setter.property)->kind == ValueKind::kBrush)
                                        {
                                            XamlResource inlineBrush{"", brush, XamlResource::Type::kValue,
                                                                     ValueKind::kBrush, setter.home};
                                            ThemedResourceValue resolved{ValueKind::kBrush};
                                            bool valid = true;
                                            for (int theme = 0; theme < 2; ++theme)
                                            {
                                                std::vector<std::string> brushChain;
                                                const auto v = resolve(inlineBrush, theme,
                                                                       setter.element->nameSpan, brushChain);
                                                if (!v)
                                                {
                                                    valid = false;
                                                    break;
                                                }
                                                resolved.values[theme] = *v;
                                            }
                                            if (valid)
                                                value = resolved, live = true;
                                        }
                        }
                        if (!value)
                        {
                            m_scope->build.push_back("    " + destination + ".supported = false;\n");
                            continue;
                        }
                        m_scope->build.push_back(
                            "    " + destination + ".setters.push_back({" + cppString(setter.property) +
                            ", {" + cppValue(value->values[0]) + ", " + cppValue(value->values[1]) + "}, " +
                            (live ? "true" : "false") + "});\n");
                    }
                    continue;
                }
                std::array<PropertyValue, 2> resolved;
                bool valid = true;
                for (int theme = 0; theme < 2; ++theme)
                {
                    std::vector<std::string> valueChain;
                    const auto value = resolve(resource, theme, resource.element->nameSpan, valueChain);
                    if (!value)
                    {
                        valid = false;
                        break;
                    }
                    resolved[theme] = *value;
                }
                if (valid)
                    m_scope->build.push_back("    " + variable + "->values[" + cppString(key) + "] = {{" +
                                             cppValue(resolved[0]) + ", " + cppValue(resolved[1]) + "}};\n");
            }
            for (auto* merged : current->merged)
            {
                const auto nested = emitDictionary(merged);
                m_scope->build.push_back("    " + variable + "->merged.push_back(" + nested + ");\n");
            }
            for (const auto& [name, themed] : current->themes)
            {
                if (name != "Default" && name != "Light" && name != "Dark")
                    continue;
                const auto nested = emitDictionary(themed);
                const std::string target =
                    name == "Default" ? "defaultTheme" : "themes[" + std::to_string(name == "Dark") + "]";
                m_scope->build.push_back("    " + variable + "->" + target + " = " + nested + ";\n");
            }
            return variable;
        };
        const auto dictionaryVariable = emitDictionary(dictionary);
        m_scope->build.push_back(application ? "    m_ui.resources().setApplication(std::move(*" +
                                                   dictionaryVariable + "));\n"
                                             : "    m_ui.resources().setScope(" + elementExpr(index) +
                                                   ", std::move(*" + dictionaryVariable + "));\n");
        m_scope->build.push_back("    }\n");
    };
    if (index == 0 && !m_scope->isControlTemplate() && !m_scope->isItemTemplate())
        publish(m_application, true);
    for (const auto* child : objectChildren(e))
        if (child->propertyElement && child->property == "Resources")
        {
            publish(m_dictionary, false);
            break;
        }
}

std::pair<const XamlElement*, XamlDictionary*>
XamlCompiler::applyStyle(const XamlElement& e, std::size_t index, const SchemaType& type)
{
    publishResources(e, index);

    std::vector<std::string> chain;
    const XamlStyle* def = nullptr;
    std::optional<XamlStyle> local;
    Span where = e.nameSpan;
    if (const auto* a = e.attribute("", "Style"))
    {
        where = a->span;
        if (!a->markup || (a->markup->kind != "StaticResource" && a->markup->kind != "ThemeResource"))
        {
            error(a->valueSpan, "Style takes a {StaticResource} naming a Style");
            return {};
        }
        const auto& key = resourceKey(*a->markup);
        XamlResource* r = lookup(key.value.text, m_dictionary, 0);
        if (!r || r->type != XamlResource::Type::kStyle)
        {
            error(key.value.span, "resource '" + key.value.text + "' is not a Style in this scope");
            return {};
        }
        def = style(*r, key.value.span, chain);
    }
    else
    {
        for (const XamlElement* c : objectChildren(e))
            if (c->propertyElement && c->owner == type.name && c->property == "Style")
            {
                const auto styles = objectChildren(*c);
                if (styles.size() != 1 || !presentation(*styles[0], "Style"))
                {
                    error(c->nameSpan, "'" + std::string(type.name) + ".Style' contains one Style");
                    return {};
                }
                where = c->nameSpan;
                local = inlineStyle(*styles[0], m_dictionary, c->nameSpan, chain);
                if (local)
                    def = &*local;
            }
        if (!def && !local)
            if (XamlResource* implicit = lookup(type.name, m_dictionary, 0);
                implicit && implicit->type == XamlResource::Type::kStyle)
                def = style(*implicit, e.nameSpan, chain);
    }
    if (!def)
        return {};
    if (def->target != type.name)
    {
        error(where,
              "the style targets '" + def->target + "', the element is a '" + std::string(type.name) + "'");
        return {};
    }
    for (const auto& setter : def->setters)
    {
        const auto* property = findSchemaProperty(setter.property);
        const auto* value = setter.element->attribute("", "Value");
        std::optional<ThemedResourceValue> v;
        bool live = false;
        if (value)
        {
            v = literalOrResource(*value, setter.home, *property);
            live = value->markup && value->markup->kind == "ThemeResource";
        }
        else
        {
            for (const XamlElement* p : objectChildren(*setter.element))
                if (p->propertyElement && p->property == "Value")
                    for (const XamlElement* brush : objectChildren(*p))
                        if (presentation(*brush, "SolidColorBrush") && property->kind == ValueKind::kBrush)
                        {
                            XamlResource inlineBrush{"", brush, XamlResource::Type::kValue, ValueKind::kBrush,
                                                     setter.home};
                            ThemedResourceValue t{ValueKind::kBrush};
                            bool ok = true;
                            for (int theme = 0; theme < 2 && ok; ++theme)
                            {
                                std::vector<std::string> brushChain;
                                const auto resolved =
                                    resolve(inlineBrush, theme, setter.element->nameSpan, brushChain);
                                ok = resolved.has_value();
                                if (ok)
                                    t.values[theme] = *resolved;
                            }
                            if (ok)
                                v = t, live = true;
                        }
            if (!v)
                errorIn(setter.home, setter.element->nameSpan, where,
                        "Setter '" + setter.property + "' needs a Value");
        }
        if (!v)
            continue;
        if (setter.home && setter.home->file.empty())
            m_scope->build.push_back(line(setter.element->nameSpan));
        setValue(index, setter.property, *v, live, "Style");
        m_scope->build.push_back("    m_ui.resources().recordStyleSetter(" + elementExpr(index) + ", p." +
                                 memberOf(setter.property) + ");\n");
    }
    return {def->templateElement, def->templateHome};
}

// ---- ControlTemplate ------------------------------------------------------------------

void XamlCompiler::instantiateTemplate(const XamlElement& owner, std::size_t index, const SchemaType& type,
                                       const XamlElement& controlTemplate, XamlDictionary* home)
{
    (void)owner;
    if (type.name != "Button")
    {
        error(controlTemplate.nameSpan, "ControlTemplate is supported for Button in this subset, not for '" +
                                            std::string(type.name) + "'");
        return;
    }
    const auto* target = controlTemplate.attribute("", "TargetType");
    if (!target || target->value != type.name)
    {
        error(target ? target->valueSpan : controlTemplate.nameSpan,
              "the ControlTemplate needs TargetType=\"" + std::string(type.name) + "\" to template a " +
                  std::string(type.name));
        return;
    }
    std::string cls;
    if (const auto it = m_controlTemplateClasses.find(&controlTemplate); it != m_controlTemplateClasses.end())
    {
        cls = it->second;
    }
    else
    {
        const auto roots = objectChildren(controlTemplate);
        if (roots.size() != 1 || roots[0]->propertyElement)
        {
            error(controlTemplate.nameSpan, "a ControlTemplate has exactly one root element");
            return;
        }
        auto scope = std::make_unique<XamlScope>();
        scope->kind = XamlScope::Kind::kControlTemplate;
        scope->cls = std::format("ControlTemplate{}", m_templates.size());
        scope->rootType = std::string(type.name); // the templated type, for TemplateBinding
        XamlScope* outer = m_scope;
        XamlDictionary* outerDictionary = m_dictionary;
        m_scope = scope.get();
        m_dictionary = home;
        cls = scope->cls;
        m_controlTemplateClasses[&controlTemplate] = cls;
        m_templates.push_back(std::move(scope));
        element(*roots[0], "owner");
        if (m_scope->visualStateGroups)
            visualStates(*m_scope->visualStateGroups);
        m_scope = outer;
        m_dictionary = outerDictionary;
    }
    const std::size_t k = m_scope->controlTemplates.size();
    m_scope->controlTemplates.push_back(cls);
    m_scope->build.push_back(
        line(controlTemplate.nameSpan) +
        std::format("    m_template{} = std::make_unique<{}>(m_ui, {});\n", k, cls, elementExpr(index)));
    // OnApplyTemplate: a controller method void OnApplyTemplate(velora::Framework&, velora::ElementId).
    if (m_controller && !m_scope->isControlTemplate())
        for (const auto& m : m_controller->methods)
            if (m.name == "OnApplyTemplate" && m.access == Access::kPublic && !m.isStatic &&
                m.signature.find("Framework") != std::string::npos &&
                m.signature.find("ElementId") != std::string::npos)
                m_scope->build.push_back(
                    std::format("    m_controller.OnApplyTemplate(m_ui, {});\n", elementExpr(index)));
}

void XamlCompiler::templateBinding(std::size_t index, const SchemaType& type, const SchemaProperty& property,
                                   const MarkupExtension& m)
{
    if (!m_scope->isControlTemplate())
    {
        error(m.span, "{TemplateBinding} is only valid inside a ControlTemplate");
        return;
    }
    const MarkupArgument* argument = m.positional ? &*m.positional : m.argument("Property");
    if (!argument)
    {
        error(m.span, "{TemplateBinding} needs a property name");
        return;
    }
    const std::string& name = argument->value.text;
    const auto* ownerType = findSchemaType(m_scope->rootType);
    const auto* source = findSchemaProperty(name);
    if (!source || !hasSchemaProperty(*ownerType, name))
    {
        error(argument->value.span,
              "'" + m_scope->rootType + "' has no property '" + name + "' to bind in its template");
        return;
    }
    if (source->kind != property.kind && !(numeric(source->kind) && numeric(property.kind)))
    {
        error(argument->value.span, std::string(type.name) + "." + std::string(property.name) + " is a " +
                                        std::string(kindName(property.kind)) + ", " + m_scope->rootType +
                                        "." + name + " is a " + std::string(kindName(source->kind)));
        return;
    }
    m_scope->build.push_back(line(m.span) + std::format("    m_ui.templateBind({}, p.{}, m_owner, p.{});\n",
                                                        elementExpr(index), memberOf(property.name),
                                                        memberOf(name)));
}

} // namespace velora
