#include "velora/runtime/BindingEngine.hpp"

#include <format>
#include <optional>

namespace velora
{
namespace
{

std::optional<PropertyValue> toProperty(const ReflectValue& v, ValueKind kind)
{
    switch (kind)
    {
    case ValueKind::kString:
        if (v.kind() == ReflectValueKind::kString)
            return PropertyValue(std::string(v.asString()));
        if (v.kind() == ReflectValueKind::kI64)
            return PropertyValue(std::to_string(v.asI64()));
        if (v.kind() == ReflectValueKind::kF64)
            return PropertyValue(std::format("{}", v.asF64()));
        if (v.kind() == ReflectValueKind::kBool)
            return PropertyValue(std::string(v.asBool() ? "True" : "False"));
        break;
    case ValueKind::kDouble:
        if (v.kind() == ReflectValueKind::kF64)
            return PropertyValue(v.asF64());
        if (v.kind() == ReflectValueKind::kI64)
            return PropertyValue(static_cast<double>(v.asI64()));
        break;
    case ValueKind::kInteger:
    case ValueKind::kBrush:
        if (v.kind() == ReflectValueKind::kI64)
            return PropertyValue(v.asI64());
        break;
    case ValueKind::kBoolean:
    case ValueKind::kVisibility:
        if (v.kind() == ReflectValueKind::kBool)
            return PropertyValue(v.asBool());
        break;
    default:
        break;
    }
    return std::nullopt;
}

// A reflected value as a property value of its own kind, for a converter's input.
std::optional<PropertyValue> natural(const ReflectValue& v)
{
    switch (v.kind())
    {
    case ReflectValueKind::kString:
        return PropertyValue(std::string(v.asString()));
    case ReflectValueKind::kI64:
        return PropertyValue(v.asI64());
    case ReflectValueKind::kF64:
        return PropertyValue(v.asF64());
    case ReflectValueKind::kBool:
        return PropertyValue(v.asBool());
    default:
        return std::nullopt;
    }
}

std::unordered_map<std::string, std::function<std::shared_ptr<const IValueConverter>()>>& converters()
{
    static std::unordered_map<std::string, std::function<std::shared_ptr<const IValueConverter>()>> registry;
    return registry;
}

std::vector<std::string> splitPath(std::string_view path)
{
    std::vector<std::string> out;
    for (std::size_t start = 0; start < path.size();)
    {
        const auto dot = std::min(path.find('.', start), path.size());
        out.emplace_back(path.substr(start, dot - start));
        start = dot + 1;
    }
    return out;
}

} // namespace

void registerValueConverter(std::string className,
                            std::function<std::shared_ptr<const IValueConverter>()> factory)
{
    converters().insert_or_assign(std::move(className), std::move(factory));
}

std::shared_ptr<const IValueConverter> createValueConverter(std::string_view className)
{
    const auto it = converters().find(std::string(className));
    return it == converters().end() ? nullptr : it->second();
}

BindingEngine::BindingEngine(ElementTree& tree, const PropertyDescriptor& dataContext)
    : m_tree(tree), m_dataContext(dataContext)
{
}
BindingEngine::~BindingEngine() = default;

void BindingEngine::bind(ElementId element, const PropertyDescriptor& target, ValueKind kind,
                         std::string_view path, BindingMode mode, BindingOptions options)
{
    auto [it, first] = m_bindings.try_emplace(element);
    if (first)
        m_tree.onRemoved(element, [this, element] { m_bindings.erase(element); });
    auto b = std::make_shared<Binding>();
    b->element = element;
    b->target = &target;
    b->kind = kind;
    b->mode = mode;
    b->options = std::move(options);
    b->path = path;
    b->names = splitPath(path);
    for (const auto& n : b->names)
        b->ids.push_back(propertyId(n));
    b->cache.resize(b->names.size());
    auto& binding = *it->second.emplace_back(std::move(b));
    evaluate(binding);
}

void BindingEngine::dataContextChanged(ElementId root)
{
    std::vector<ElementId> pending{root};
    std::vector<std::shared_ptr<Binding>> affected;
    while (!pending.empty())
    {
        const ElementId e = pending.back();
        pending.pop_back();
        if (const auto it = m_bindings.find(e); it != m_bindings.end())
            for (const auto& b : it->second)
                if (!(e == root && b->target == &m_dataContext))
                    affected.push_back(b); // reads the parent's
        const auto& children = m_tree.children(e);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    for (const auto& b : affected)
        evaluate(*b); // OneTime bindings, too, follow a new DataContext
}

void BindingEngine::writeBack(ElementId element, const PropertyDescriptor& target)
{
    const auto it = m_bindings.find(element);
    if (it == m_bindings.end())
        return;
    const auto bindings = it->second; // callbacks may remove the target and its bindings
    for (const auto& b : bindings)
    {
        if (!m_tree.contains(element))
            return;
        if (b->writing || b->evaluating || b->target != &target || b->mode != BindingMode::kTwoWay ||
            !b->leafOwner || b->cache.empty())
            continue;
        const auto* a = b->cache.back().accessor;
        if (!a || !a->set)
        {
            fail(*b, "'" + b->names.back() + "' is read-only");
            continue;
        }
        // Text, checked state and range values are the TwoWay targets.
        PropertyValue value = m_tree.property(element, target);
        if (b->options.converter)
        {
            auto back = b->options.converter->convertBack(value, b->options.converterParameter);
            if (!back)
                continue; // the converter cannot write this value back; the source keeps its own
            value = std::move(*back);
        }
        ReflectValue written;
        if (value.isString())
            written = ReflectValue(value.asString());
        else if (value.isBool())
            written = ReflectValue(value.asBool());
        else if (value.isDouble())
            written = ReflectValue(value.asDouble());
        else
            continue;
        b->writing = true;
        const bool accepted = a->set(b->leafOwner.object, written);
        b->writing = false;
        if (!accepted)
            fail(*b, "'" + b->names.back() + "' does not accept " +
                         (value.isString() ? "text"
                          : value.isBool() ? "a boolean"
                                           : "a number"));
        evaluate(*b); // shows the value the source kept (it may coerce)
    }
}

const BindingEngine::Binding::Step& BindingEngine::step(Binding& b, std::size_t index,
                                                        const ObjectRef& object)
{
    auto& cached = b.cache[index];
    if (cached.type && *cached.type == *object.type)
        return cached;
    ++m_lookups;
    const auto* table = findAccessors(*object.type);
    cached = {object.type, table, table ? table->find(b.names[index]) : nullptr};
    return cached;
}

void BindingEngine::evaluate(Binding& b)
{
    if (b.writing || b.evaluating || !m_tree.contains(b.element))
        return;
    // A target change raises control events; handlers may remove the element.
    const auto lifetime = b.shared_from_this();
    struct Evaluation
    {
        bool& active;
        explicit Evaluation(bool& value) : active(value) { active = true; }
        ~Evaluation() { active = false; }
    } evaluation(b.evaluating);
    ++m_evaluations;
    b.subscriptions.clear();
    b.leafOwner = {};
    const ElementId contextElement = b.target == &m_dataContext ? m_tree.parent(b.element) : b.element;
    const PropertyValue& context =
        contextElement ? m_tree.property(contextElement, m_dataContext) : m_dataContext.defaultValue;
    if (!context.isObject() || !context.asObject())
    {
        clearTarget(b); // no DataContext yet: the target stays unset, which is not an error
        return;
    }
    if (b.names.empty())
    {
        if (b.kind != ValueKind::kObject)
            return fail(
                b, "an empty Binding path yields the DataContext object, which only DataContext accepts");
        if (!b.writing)
            m_tree.setProperty(b.element, *b.target, context, PropertySource::kLocal);
        return;
    }
    ObjectRef object{context.asObject().object, context.asObject().type};
    for (std::size_t i = 0; i < b.names.size(); ++i)
    {
        const auto& s = step(b, i, object);
        const auto* a = s.accessor;
        if (!s.table)
            return fail(b, "the source object's type has no reflection accessors (reflect it with velorac)");
        if (!a)
            return fail(b, "'" + std::string(s.table->name) + "' has no property '" + b.names[i] + "'");
        if (auto* observable = s.table->observable ? s.table->observable(object.object) : nullptr;
            observable && b.mode != BindingMode::kOneTime)
            b.subscriptions.push_back(
                observe(*observable, b.ids[i], [this, weak = b.weak_from_this()]
                {
                    if (const auto binding = weak.lock())
                        evaluate(*binding);
                }));
        if (i + 1 < b.names.size())
        {
            if (!a->getObject)
                return fail(b, "'" + b.names[i] + "' is not an object, so '" + b.names[i + 1] +
                                   "' cannot follow it");
            object = a->getObject(object.object);
            if (!object)
                return clearTarget(b);
            continue;
        }
        b.leafOwner = object;
        std::optional<PropertyValue> value;
        if (b.kind == ValueKind::kObject)
        {
            if (a->getObject)
            {
                const auto o = a->getObject(object.object);
                value = PropertyValue(ObjectRef{o.object, o.type, nullptr});
            }
        }
        else if (a->get && b.options.converter)
        {
            if (const auto input = natural(a->get(object.object)))
                value = b.options.converter->convert(*input, b.options.converterParameter);
            if (!value)
                return clearTarget(b); // the converter declined: fallback, not an error
        }
        else if (a->get)
        {
            value = toProperty(a->get(object.object), b.kind);
        }
        if (!value)
            return fail(b, "'" + b.names[i] + "' cannot be shown as " + std::string(b.target->name));
        if (!b.writing)
            m_tree.setProperty(b.element, *b.target, std::move(*value), PropertySource::kLocal);
    }
}

void BindingEngine::fail(Binding& b, const std::string& message)
{
    m_errors.push_back("{Binding " + b.path + "} on " + std::string(m_tree.type(b.element)) + "." +
                       b.target->name + ": " + message);
    clearTarget(b);
}

void BindingEngine::clearTarget(Binding& b)
{
    if (b.writing)
        return;
    if (b.options.fallback)
        m_tree.setProperty(b.element, *b.target, *b.options.fallback, PropertySource::kLocal);
    else
        m_tree.clearProperty(b.element, *b.target, PropertySource::kLocal);
}

BindingStats BindingEngine::stats() const
{
    BindingStats s;
    for (const auto& [element, list] : m_bindings)
        for (const auto& b : list)
        {
            ++s.bindings;
            for (const auto& sub : b->subscriptions)
                s.subscriptions += sub.active();
        }
    s.evaluations = m_evaluations;
    s.accessorLookups = m_lookups;
    return s;
}

} // namespace velora
