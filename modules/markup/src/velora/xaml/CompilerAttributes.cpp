// velorac: attributes: literals, resources, x:Bind, {Binding} and event handlers.
#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <string_view>

namespace velora
{

namespace
{
// Targets that report changes, so TwoWay bindings can write them back: the events that
// signal a change and the PropertyValue accessor that reads the new value.
struct TwoWayTarget
{
    std::string_view type, property, accessor;
    std::initializer_list<std::string_view> events;
};
constexpr std::string_view kTwoWayTargets =
    "TextBox.Text, CheckBox.IsChecked, RadioButton.IsChecked, ToggleSwitch.IsOn or Slider.Value";

const TwoWayTarget* twoWayTarget(std::string_view type, std::string_view property)
{
    static const TwoWayTarget targets[] = {
        {"TextBox", "Text", "asString", {"TextChanged"}},
        {"CheckBox", "IsChecked", "asBool", {"Checked", "Unchecked"}},
        {"RadioButton", "IsChecked", "asBool", {"Checked", "Unchecked"}},
        {"ToggleSwitch", "IsOn", "asBool", {"Toggled"}},
        {"Slider", "Value", "asDouble", {"ValueChanged"}},
    };
    for (const auto& t : targets)
        if (t.type == type && t.property == property)
            return &t;
    return nullptr;
}
} // namespace

void XamlCompiler::setProperty(std::size_t index, const SchemaType* type, const XamlAttribute& a,
                               std::set<std::string>& assigned)
{
    switch (a.kind)
    {
    case XamlAttributeKind::kNamespaceDeclaration:
    case XamlAttributeKind::kXmlAttribute:
    case XamlAttributeKind::kIgnored:
        return;
    case XamlAttributeKind::kDirective:
        if (a.name.local == "Name")
        {
            if (!identifier(a.value))
                error(a.valueSpan, "x:Name '" + a.value + "' is not a C++ identifier");
            else if (const auto [it, added] = m_scope->names.emplace(a.value, std::pair{a.span, index});
                     !added)
                error(a.span, std::format("duplicate x:Name '{}' (first declared at {}:{})", a.value,
                                          it->second.first.begin.line, it->second.first.begin.column));
            else
            {
                m_scope->nameOrder.push_back(a.value);
                m_scope->nameTypes[a.value] = type->name;
                m_scope->build.push_back(
                    std::format("    m_names.{} = velora::TypedElementId<velora::ElementKind::k{}>({});\n",
                                a.value, type->name, elementExpr(index)));
            }
        }
        else if (a.name.local == "Key")
        {
            error(a.span, "x:Key is only valid on resources");
        }
        else if (a.name.local == "DataType")
        {
            error(a.span, "x:DataType is only valid on a DataTemplate");
        }
        else if (a.name.local != "Class" && a.name.local != "Uid")
        {
            error(a.span, "x:" + a.name.local + " is not supported by velorac");
        }
        return;
    default:
        break;
    }
    if (!a.name.condition.empty())
    {
        error(a.span, "conditional XAML namespaces are not supported by velorac");
        return;
    }
    const std::string name = a.name.local;
    if (hasSchemaEvent(*type, name))
    {
        if (m_scope->isControlTemplate())
            error(a.span, "event handlers are not supported inside a ControlTemplate");
        else
            handler(index, name, a);
        return;
    }
    const auto* property = findSchemaProperty(name);
    if (!property || !hasSchemaProperty(*type, name))
    {
        error(a.span, "'" + std::string(type->name) + "' has no property or event '" + name + "'");
        return;
    }
    if (!assigned.insert(name).second)
    {
        error(a.span, "'" + name + "' is set more than once");
        return;
    }
    if (a.markup)
    {
        const auto& m = *a.markup;
        if (m_scope->isControlTemplate() && (m.kind == "x:Bind" || m.kind == "Binding"))
        {
            error(m.span,
                  "{" + m.kind + "} is not supported inside a ControlTemplate; use {TemplateBinding}");
            return;
        }
        if (m.kind == "x:Bind")
            return bind(index, type, *property, a);
        if (m.kind == "Binding")
            return binding(index, type, *property, m);
        if (m.kind == "TemplateBinding")
            return templateBinding(index, *type, *property, m);
        if (m.kind == "x:Null")
            return;
        if (m.kind == "StaticResource" || m.kind == "ThemeResource")
        {
            m_scope->build.push_back(line(a.span));
            if (const auto v = resourceValue(m, m_dictionary, property->kind, name))
                setValue(index, name, *v, m.kind == "ThemeResource", "");
            return;
        }
        error(m.span, "{" + m.kind + "} is not supported by velorac yet");
        return;
    }
    auto parsed = parseValue(*property, a.value);
    if (auto* message = std::get_if<std::string>(&parsed))
    {
        error(a.valueSpan, *message);
        return;
    }
    const auto& value = std::get<PropertyValue>(parsed);
    if (value.empty())
        return;
    m_scope->build.push_back(line(a.span) + std::format("    m_ui.set({}, p.{}, {});\n", elementExpr(index),
                                                        memberOf(name), cppValue(value)));
}

std::optional<XamlCompiler::BoundPath> XamlCompiler::boundPath(const MarkupExtension& m, bool twoWay)
{
    if (!m.bind)
        return std::nullopt; // the parser already reported the syntax error
    const auto& b = *m.bind;
    if (b.call)
    {
        error(b.span, "x:Bind function calls are not supported by velorac yet");
        return std::nullopt;
    }
    std::string path;
    for (const auto& s : b.path.steps)
    {
        if (s.kind == BindStep::Kind::kIndex)
        {
            error(s.span, "x:Bind indexers are not supported by velorac yet");
            return std::nullopt;
        }
        path += (path.empty() ? "" : ".") + s.name;
    }
    for (const auto& a : m.named)
        if (a.name != "Mode" && a.name != "Path")
            error(a.nameSpan, "x:Bind " + a.name + " is not supported by velorac yet");
    const auto r = m_catalog.resolve(m_scope->rootType, path, twoWay);
    if (!r.ok)
    {
        error(b.path.span, r.error);
        return std::nullopt;
    }
    BoundPath out{m_scope->rootExpr, r.type, r.steps};
    bool pointer = false;
    for (const auto& s : r.steps)
    {
        out.owners.push_back(out.getterExpr);
        out.getterExpr += (pointer ? "->" : ".") + (s.isProperty ? s.getter + "()" : s.member);
        pointer = isPointer(s.type);
        if (const auto* t = m_catalog.find(s.type))
            useType(t->qualifiedName);
    }
    return out;
}

void XamlCompiler::addBinding(const std::string& mode, const BoundPath& path, std::string apply, bool guarded)
{
    if (mode == "OneTime")
    {
        m_scope->once.push_back(std::move(apply));
        return;
    }
    XamlLiveBinding live{std::move(apply), path.owners, {}, guarded};
    for (const auto& s : path.steps)
        live.names.push_back(s.member);
    m_scope->live.push_back(std::move(live));
}

void XamlCompiler::bind(std::size_t index, const SchemaType* type, const SchemaProperty& property,
                        const XamlAttribute& a)
{
    const auto& m = *a.markup;
    const auto* modeArgument = m.argument("Mode");
    const std::string mode = modeArgument ? modeArgument->value.text : "OneTime";
    const bool twoWay = mode == "TwoWay";
    const TwoWayTarget* target = twoWayTarget(type->name, property.name);
    if (twoWay && !target)
    {
        error(modeArgument->value.span,
              "TwoWay binding needs a target that reports changes; in this subset that is " +
                  std::string(kTwoWayTargets));
        return;
    }
    const auto path = boundPath(m, twoWay);
    if (!path)
        return;
    const auto value = convert(path->getterExpr, path->type, property.kind);
    if (!value)
    {
        error(m.bind->path.span,
              "x:Bind path is " + bareType(path->type) + ", " + std::string(type->name) + "." +
                  std::string(property.name) + " needs " +
                  (property.kind == ValueKind::kObject ? "a reflected object by non-const reference"
                                                       : "a " + std::string(kindName(property.kind))));
        return;
    }
    const std::size_t k = m_scope->live.size();
    addBinding(mode, *path,
               line(a.span) + std::format("    m_ui.set({}, p.{}, {});\n", elementExpr(index),
                                          memberOf(property.name), *value),
               twoWay);
    if (!twoWay)
        return;
    // Write back after every change the target reports. Notifications the setter raises for
    // this binding are ignored while it writes; re-applying afterwards shows the value the
    // source kept (an unchanged value raises no further change event).
    const auto& last = path->steps.back();
    const bool pointer = path->steps.size() > 1 && isPointer(path->steps[path->steps.size() - 2].type);
    const std::string& owner = path->owners.back();
    const std::string current = std::format("m_ui.tree().property({}, *m_ui.props().{}).{}()",
                                            elementExpr(index), memberOf(property.name), target->accessor);
    const std::string write =
        last.isProperty ? std::format("{}{}{}({})", owner, pointer ? "->" : ".", last.setter, current)
                        : std::format("{}{}{} = {}", owner, pointer ? "->" : ".", last.member, current);
    for (const auto event : target->events)
    {
        auto& statements = m_scope->handlers[{index, std::string(event)}];
        statements.insert(statements.begin(),
                          line(a.span) + std::format("        m_writing{0} = true;\n        {1};\n        "
                                                     "m_writing{0} = false;\n        apply{0}();\n",
                                                     k, write));
    }
}

// {Binding Path, Mode}: resolved at run time against the DataContext (BindingEngine).
void XamlCompiler::binding(std::size_t index, const SchemaType* type, const SchemaProperty& property,
                           const MarkupExtension& m)
{
    for (const auto& a : m.named)
        if (a.name != "Path" && a.name != "Mode")
        {
            error(a.nameSpan, "{Binding} " + a.name + " is not supported by velorac yet");
            return;
        }
    const MarkupArgument* pathArgument = m.positional ? &*m.positional : m.argument("Path");
    const std::string path = pathArgument ? pathArgument->value.text : "";
    if (pathArgument && !dottedPath(path))
    {
        error(pathArgument->value.span, "{Binding} paths are dotted property names in this subset");
        return;
    }
    if (path.empty() && property.kind != ValueKind::kObject)
    {
        error(m.span,
              "{Binding} without a path yields the DataContext object, which only DataContext accepts");
        return;
    }
    const auto* modeArgument = m.argument("Mode");
    const std::string mode = modeArgument ? modeArgument->value.text : "OneWay";
    if (mode == "TwoWay" && !twoWayTarget(type->name, property.name))
    {
        error(modeArgument->value.span,
              "TwoWay binding needs a target that reports changes; in this subset that is " +
                  std::string(kTwoWayTargets));
        return;
    }
    switch (property.kind)
    {
    case ValueKind::kThickness:
    case ValueKind::kGridLengths:
    case ValueKind::kEnum:
        error(m.span, "{Binding} cannot set " + std::string(property.name) + " (" +
                          std::string(kindName(property.kind)) + ") in this subset");
        return;
    default:
        break;
    }
    m_scope->build.push_back(line(m.span) +
                             std::format("    m_ui.bind({}, p.{}, {}, velora::BindingMode::k{});\n",
                                         elementExpr(index), memberOf(property.name), cppString(path), mode));
}

void XamlCompiler::handler(std::size_t index, std::string_view event, const XamlAttribute& a)
{
    if (a.markup || !identifier(a.value))
    {
        error(a.valueSpan, "an event handler is the name of a controller method");
        return;
    }
    const MethodMetadata* method = nullptr;
    for (const auto& m : m_controller->methods)
        if (m.name == a.value && m.constructor == ConstructorKind::kNone)
            method = &m;
    if (!method || method->access != Access::kPublic || method->isStatic)
    {
        error(a.valueSpan, "'" + m_class.qualified() + "' has no public reflected method '" + a.value +
                               "' (declare it with cfunc)");
        return;
    }
    // void Name() everywhere; in a DataTemplate also void Name(Item&) receiving the container's item.
    const auto open = method->signature.find(a.value + "(");
    const auto close = method->signature.find(')', open);
    const std::string parameters =
        open == std::string::npos || close == std::string::npos
            ? "?"
            : method->signature.substr(open + a.value.size() + 1, close - open - a.value.size() - 1);
    std::string call;
    if (parameters.empty())
    {
        call = "m_controller." + a.value + "()";
    }
    else if (m_scope->isItemTemplate() && isMutableReference(parameters))
    {
        const auto* t = m_catalog.find(bareType(parameters), m_class.qualified());
        if (t && t->qualifiedName == m_scope->rootType)
            call = "m_controller." + a.value + "(*m_item)";
    }
    if (call.empty())
    {
        error(a.valueSpan, "'" + method->signature + "' does not match the " + std::string(event) +
                               " handler signature " + std::string(findSchemaEvent(event)->signature) +
                               (m_scope->isItemTemplate() ? " or void(" + m_scope->rootType + "&)" : ""));
        return;
    }
    m_scope->handlers[{index, std::string(event)}].push_back(line(a.span) + "        " + call + ";\n");
}

} // namespace velora
