// velorac: custom controls, items hosts (ListView + DataTemplate) and property elements.
#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <format>

namespace velora
{

void XamlCompiler::customElement(const XamlElement& e, std::size_t index, const TypeMetadata& type,
                                 const std::string& parentExpr)
{
    useType(type.qualifiedName);
    const auto view = m_views.find(type.qualifiedName);
    if (view == m_views.end())
    {
        std::string xamlName = type.qualifiedName;
        for (auto at = xamlName.find("::"); at != std::string::npos; at = xamlName.find("::", at))
            xamlName.replace(at, 2, ".");
        error(e.nameSpan,
              type.qualifiedName +
                  " has no XAML view in this build; custom controls are UserControls with x:Class=\"" +
                  xamlName + "\"");
        return;
    }
    if (!type.constructible())
    {
        error(e.nameSpan,
              type.qualifiedName +
                  " is not default-constructible; a custom control needs a public default constructor");
        return;
    }
    m_dependencies.insert(view->second.file);
    m_viewIncludes.insert(view->second.header);
    XamlDictionary* const outerDictionary = m_dictionary;
    elementResources(e);
    const std::size_t k = m_scope->children.size();
    m_scope->children.push_back(type.qualifiedName);
    const std::string controller = std::format("m_child{}", k), viewMember = std::format("m_childView{}", k);
    m_scope->build.push_back(line(e.nameSpan) + std::format("    {} = std::make_unique<::{}>();\n",
                                                            controller, type.qualifiedName));
    // A custom control that does not notify its own changes is refreshed after each write.
    const std::string refreshChild =
        std::format("    if constexpr (!std::is_base_of_v<velora::Observable, ::{}>) {}->updateBindings();\n",
                    type.qualifiedName, viewMember);
    std::vector<const XamlAttribute*> frameworkAttributes;
    for (const auto& a : e.attributes)
    {
        if (a.kind == XamlAttributeKind::kDirective && a.name.local == "Name")
        {
            frameworkAttributes.push_back(&a);
            continue;
        }
        if (a.kind != XamlAttributeKind::kProperty && a.kind != XamlAttributeKind::kAttachedProperty)
            continue;
        const PropertyMetadata* property = nullptr;
        for (const auto& p : type.properties)
            if (p.name == a.name.local)
                property = &p;
        if (!property)
        {
            if (hasSchemaProperty(*findSchemaType("UserControl"), a.name.local))
                frameworkAttributes.push_back(&a);
            else
                error(a.span, type.qualifiedName + " has no property '" + a.name.local + "'");
            continue;
        }
        if (property->setter.empty())
        {
            error(a.span, "'" + property->symbol + "' is read-only");
            continue;
        }
        const TypeCategory c = category(property->type);
        if (a.markup && a.markup->kind == "x:Bind")
        {
            const auto* mode = a.markup->argument("Mode");
            if (mode && mode->value.text == "TwoWay")
            {
                error(mode->value.span,
                      "TwoWay binding of a custom control property is not supported; use OneWay");
                continue;
            }
            const auto path = boundPath(*a.markup, false);
            if (!path)
                continue;
            const TypeCategory source = category(path->type);
            const bool compatible =
                source == c || (c == TypeCategory::kFloating && source == TypeCategory::kInteger);
            if (!compatible)
            {
                error(a.markup->bind->path.span, "x:Bind path is " + bareType(path->type) + ", " +
                                                     property->symbol + " is " + bareType(property->type));
                continue;
            }
            const std::string value =
                c == TypeCategory::kString ? "std::string(" + path->getterExpr + ")" : path->getterExpr;
            addBinding(mode ? mode->value.text : "OneTime", *path,
                       line(a.span) + std::format("    {}->{}({});\n", controller, property->setter, value) +
                           refreshChild);
            continue;
        }
        if (a.markup)
        {
            error(a.markup->span,
                  "only literal values and x:Bind are supported for custom control properties");
            continue;
        }
        std::string value;
        switch (c)
        {
        case TypeCategory::kString:
            value = "std::string(" + cppString(a.value) + ")";
            break;
        case TypeCategory::kBoolean:
            if (a.value == "True" || a.value == "true")
                value = "true";
            else if (a.value == "False" || a.value == "false")
                value = "false";
            break;
        case TypeCategory::kInteger:
        case TypeCategory::kFloating:
            if (const auto n = parseNumber(a.value);
                n && (c == TypeCategory::kFloating || *n == static_cast<double>(static_cast<long long>(*n))))
                value = std::format("static_cast<{}>({})", bareType(property->type), *n);
            break;
        default:
            error(a.span, "'" + property->symbol + "' has type " + property->type +
                              ", which XAML literals cannot set");
            continue;
        }
        if (value.empty())
        {
            error(a.valueSpan, "'" + a.value + "' is not a valid " + bareType(property->type) + " for " +
                                   property->symbol);
            continue;
        }
        m_scope->build.push_back(line(a.span) +
                                 std::format("    {}->{}({});\n", controller, property->setter, value));
    }
    for (const auto& child : e.children)
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && !(*nested)->ignored)
            if (!((*nested)->propertyElement && (*nested)->property == "Resources"))
                error((*nested)->nameSpan, "custom controls cannot have content in this subset");
    m_scope->build.push_back(line(e.nameSpan) +
                             std::format("    {} = std::make_unique<::{}_xaml>(m_ui, *{}, {});\n", viewMember,
                                         type.qualifiedName, controller, parentExpr));
    m_scope->build.push_back(std::format("    {} = {}->root();\n", elementExpr(index), viewMember));
    publishResources(e, index);
    m_scope->refresh.push_back(std::format("    {}->updateBindings();\n", viewMember));
    std::set<std::string> assigned;
    for (const auto* a : frameworkAttributes)
        setProperty(index, findSchemaType("UserControl"), *a, assigned);
    m_dictionary = outerDictionary;
}

// ListView: ItemsSource="{x:Bind path}" naming an ObservableVector<T>, and an ItemTemplate
// whose DataTemplate (x:DataType T) becomes a nested container class.
void XamlCompiler::items(const XamlElement& e, std::size_t index, const XamlAttribute* source,
                         const XamlElement* itemTemplate)
{
    if (!source)
        error(e.nameSpan, "'" + e.name.local + "' needs ItemsSource=\"{x:Bind ...}\"");
    if (!itemTemplate)
        error(e.nameSpan, "'" + e.name.local + "' needs a " + e.name.local + ".ItemTemplate");
    if (!source || !itemTemplate)
        return;
    if (!source->markup || source->markup->kind != "x:Bind")
    {
        error(source->valueSpan, "ItemsSource takes {x:Bind} to a velora::ObservableVector");
        return;
    }
    const auto* mode = source->markup->argument("Mode");
    if (mode && mode->value.text == "TwoWay")
    {
        error(mode->value.span, "ItemsSource cannot be bound TwoWay");
        return;
    }
    const auto path = boundPath(*source->markup, false);
    if (!path)
        return;
    const auto itemName = vectorItem(path->type);
    if (!itemName || !isMutableReference(path->type))
    {
        error(source->markup->bind->path.span,
              "ItemsSource needs a velora::ObservableVector<T>& (non-const reference); the path is " +
                  path->type);
        return;
    }
    const auto* item = m_catalog.find(*itemName, m_scope->rootType);
    if (!item)
    {
        error(source->markup->bind->path.span, "item type '" + *itemName + "' has no reflection metadata");
        return;
    }
    useType(item->qualifiedName);

    std::vector<const XamlElement*> templates;
    for (const auto& child : itemTemplate->children)
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && !(*nested)->ignored)
            templates.push_back(nested->get());
    if (templates.size() != 1 || templates[0]->name.ns != kXamlPresentationNs ||
        templates[0]->name.local != "DataTemplate")
    {
        error(itemTemplate->nameSpan, "'" + itemTemplate->owner + ".ItemTemplate' contains one DataTemplate");
        return;
    }
    const XamlElement& dataTemplate = *templates[0];
    const auto* dataType = dataTemplate.attribute(kXamlNs, "DataType");
    if (!dataType)
    {
        error(dataTemplate.nameSpan, "a DataTemplate needs x:DataType naming its item type");
        return;
    }
    if (resolveTypeName(dataType->value) != item->qualifiedName)
    {
        error(dataType->valueSpan, "x:DataType '" + dataType->value +
                                       "' does not match the ItemsSource item type " + item->qualifiedName);
        return;
    }
    std::vector<const XamlElement*> roots;
    for (const auto& child : dataTemplate.children)
    {
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && !(*nested)->ignored)
            roots.push_back(nested->get());
        else if (const auto* t = std::get_if<XamlText>(&child); t && !t->value.empty())
            error(t->span, "a DataTemplate cannot contain text");
    }
    for (const auto& a : dataTemplate.attributes)
        if (a.kind == XamlAttributeKind::kProperty ||
            (a.kind == XamlAttributeKind::kDirective && a.name.local != "DataType"))
            error(a.span, "DataTemplate has no attribute '" + a.name.local + "' in this subset");
    if (roots.size() != 1 || roots[0]->propertyElement)
    {
        error(dataTemplate.nameSpan, "a DataTemplate has exactly one root element");
        return;
    }

    // The template is its own scope: elements, names, bindings and handlers of one container.
    auto tmpl = std::make_unique<XamlScope>();
    tmpl->cls = std::format("ItemTemplate{}", m_templates.size());
    tmpl->kind = XamlScope::Kind::kItemTemplate;
    tmpl->rootType = item->qualifiedName;
    tmpl->rootExpr = "(*m_item)";
    XamlScope* outer = m_scope;
    m_scope = tmpl.get();
    m_templates.push_back(std::move(tmpl));
    element(*roots[0], "parent");
    m_scope = outer;

    const std::string& cls = m_templates.back()->cls;
    const std::size_t k = m_scope->repeaters.size();
    std::string name;
    if (const auto* xName = e.attribute(kXamlNs, "Name"); xName && identifier(xName->value))
        name = xName->value;
    m_scope->repeaters.push_back({item->qualifiedName, cls, name});
    m_scope->build.push_back(
        line(e.nameSpan) +
        std::format("    m_items{0} = std::make_unique<velora::ItemsRepeater<::{1}, {2}>>(m_ui, {3}, "
                    "[this](velora::ElementId parent) {{\n"
                    "        return std::make_unique<{2}>(m_ui, m_controller, parent);\n    }});\n",
                    k, item->qualifiedName, cls, elementExpr(index)));
    addBinding(mode ? mode->value.text : "OneTime", *path,
               line(source->span) + std::format("    m_items{}->setSource(&({}));\n", k, path->getterExpr));
    if (m_scope->isItemTemplate())
        m_scope->unbind.push_back(std::format("    m_items{}->setSource(nullptr);\n", k));
}

void XamlCompiler::propertyElement(std::size_t index, const SchemaType& type, const XamlElement& p,
                                   std::set<std::string>& assigned)
{
    if (p.property == "Resources")
        return; // handled before the element's own attributes
    if (p.owner != type.name)
    {
        error(p.nameSpan, "property element '" + p.owner + "." + p.property + "' does not belong to '" +
                              std::string(type.name) + "'");
        return;
    }
    std::vector<const XamlElement*> objects;
    for (const auto& child : p.children)
        if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child);
            nested && !(*nested)->ignored)
            objects.push_back(nested->get());
    const auto* property = findSchemaProperty(p.property);
    if (!property || !hasSchemaProperty(type, p.property))
    {
        error(p.nameSpan, "'" + std::string(type.name) + "' has no property '" + p.property + "'");
        return;
    }
    if (!assigned.insert(p.property).second)
    {
        error(p.nameSpan, "'" + p.property + "' is set more than once");
        return;
    }
    PropertyValue value;
    if (property->kind == ValueKind::kGridLengths)
    {
        const std::string item = p.property == "RowDefinitions" ? "RowDefinition" : "ColumnDefinition";
        const std::string size = p.property == "RowDefinitions" ? "Height" : "Width";
        GridLengths lengths;
        for (const XamlElement* o : objects)
        {
            if (o->name.local != item)
            {
                error(o->nameSpan, "'" + p.property + "' contains only " + item + " elements");
                continue;
            }
            const auto* a = o->attribute("", size);
            const auto g = parseGridLength(a ? a->value : "*");
            if (!g)
                error(a->valueSpan, "'" + a->value + "' is not a GridLength (use Auto, *, 2* or pixels)");
            else
                lengths.push_back(*g);
        }
        value = PropertyValue(std::move(lengths));
    }
    else if (property->kind == ValueKind::kBrush && objects.size() == 1 &&
             objects[0]->name.local == "SolidColorBrush")
    {
        XamlResource inlineBrush{"", objects[0], XamlResource::Type::kValue, ValueKind::kBrush, m_dictionary};
        ThemedResourceValue v{ValueKind::kBrush};
        for (int theme = 0; theme < 2; ++theme)
        {
            std::vector<std::string> chain;
            const auto resolved = resolve(inlineBrush, theme, objects[0]->nameSpan, chain);
            if (!resolved)
                return;
            v.values[theme] = *resolved;
        }
        setValue(index, p.property, v, true, "");
        return;
    }
    else
    {
        error(p.nameSpan,
              "property element '" + p.owner + "." + p.property + "' is not supported for this value");
        return;
    }
    if (!value.empty())
        m_scope->build.push_back(line(p.nameSpan) + std::format("    m_ui.set({}, p.{}, {});\n",
                                                                elementExpr(index), memberOf(p.property),
                                                                cppValue(value)));
}

} // namespace velora
