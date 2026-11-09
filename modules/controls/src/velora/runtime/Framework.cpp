#include "velora/runtime/FrameworkInternal.hpp"

#include <velora/controls/ElementFactory.hpp>
#include <velora/globals.hpp>

namespace velora
{
float approximateTextWidth(std::string_view text, float fontSize)
{
    float width = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        width += fontSize * (length == 1 ? 0.55f : 1.0f);
        i += length;
    }
    return width;
}

void Framework::defineProperties(PropertyRegistry& registry, const VisualProperties& visual,
                                 Properties& props)
{
    // Properties the composition bridge already defines are shared, the rest come from the schema.
    const auto define = [&](std::string_view name) -> const PropertyDescriptor*
    {
        if (name == "Opacity")
            return visual.opacity;
        if (name == "Visibility")
            return visual.visible;
        if (name == "Background")
            return visual.background;
        if (name == "CornerRadius")
            return visual.cornerRadius;
        const auto& p = *findSchemaProperty(name);
        auto value = parseValue(p, p.defaultValue);
        if (auto* error = std::get_if<std::string>(&value))
            throw std::logic_error("schema default for " + std::string(name) + ": " + *error);
        return &registry.define(std::string(name), std::get<PropertyValue>(std::move(value)),
                                flagsOf(p.flags), (p.flags & kSchemaInherited) != 0);
    };
#define VELORA_DEFINE_PROPERTY(member, name, kind, choices, value, flags) props.member = define(name);
    VELORA_UI_PROPERTIES(VELORA_DEFINE_PROPERTY)
#undef VELORA_DEFINE_PROPERTY
    props.IsPressed = &registry.define("IsPressed", PropertyValue(false), PropertyFlags::kPaint);
    props.IsFocused = &registry.define("IsFocused", PropertyValue(false), PropertyFlags::kPaint);
    props.IsPointerOver = &registry.define("IsPointerOver", PropertyValue(false), PropertyFlags::kPaint);
    props.HasTemplate = &registry.define("HasTemplate", PropertyValue(false),
                                         PropertyFlags::kMeasure | PropertyFlags::kPaint);
    props.CaretPosition = &registry.define("CaretPosition", PropertyValue(i64{0}), PropertyFlags::kPaint);
    props.SelectionAnchor = &registry.define("SelectionAnchor", PropertyValue(i64{0}), PropertyFlags::kPaint);
    props.PreeditText = &registry.define("PreeditText", PropertyValue(std::string()),
                                         PropertyFlags::kMeasure | PropertyFlags::kPaint);
    props.PreeditCursor = &registry.define("PreeditCursor", PropertyValue(i64{0}), PropertyFlags::kPaint);
    props.ScrollOffset = &registry.define("ScrollOffset", PropertyValue(0.0), PropertyFlags::kArrange);
    props.ItemCount = &registry.define("ItemCount", PropertyValue(i64{0}), PropertyFlags::kMeasure);
    props.ItemIndex = &registry.define("ItemIndex", PropertyValue(i64{0}), PropertyFlags::kArrange);
    props.HorizontalOffset =
        &registry.define("HorizontalOffset", PropertyValue(0.0), PropertyFlags::kArrange);
    props.VerticalOffset = &registry.define("VerticalOffset", PropertyValue(0.0), PropertyFlags::kArrange);
    props.ExtentWidth = &registry.define("ExtentWidth", PropertyValue(0.0), PropertyFlags::kPaint);
    props.ExtentHeight = &registry.define("ExtentHeight", PropertyValue(0.0), PropertyFlags::kPaint);
    props.ViewportWidth = &registry.define("ViewportWidth", PropertyValue(0.0), PropertyFlags::kPaint);
    props.ViewportHeight = &registry.define("ViewportHeight", PropertyValue(0.0), PropertyFlags::kPaint);
    props.ScrollBarExpanded =
        &registry.define("ScrollBarExpanded", PropertyValue(i64{0}), PropertyFlags::kPaint);
}

Framework::Framework(Compositor& compositor, TextMeasurer measure)
    : m_compositor(compositor), m_measure(std::move(measure)), m_visual(VisualProperties::define(m_registry))
{
    m_resources = std::make_unique<ResourceScopeStore>(*this);
    defineProperties(m_registry, m_visual, m_props);
#define VELORA_DEFINE_EVENT(member, name, signature)                                                         \
    m_events.member = &m_eventRegistry.define("Framework", name, signature);
    VELORA_UI_EVENTS(VELORA_DEFINE_EVENT)
#undef VELORA_DEFINE_EVENT
    for (auto [member, name] :
         {std::pair{&m_events.PointerPressed, "PointerPressed"},
          std::pair{&m_events.PointerReleased, "PointerReleased"}, std::pair{&m_events.KeyDown, "KeyDown"},
          std::pair{&m_events.GotFocus, "GotFocus"}, std::pair{&m_events.LostFocus, "LostFocus"}})
        *member = &m_eventRegistry.define("UIElement", name, "void(RoutedEventArgs&)");
    m_bindings = std::make_unique<BindingEngine>(m_tree, *m_props.DataContext);
    m_tree.subscribe(
        [this](ElementId element, const PropertyDescriptor& incoming)
        {
            if (!m_tree.contains(element))
                return;
            // Object APIs use process-wide descriptors; Framework uses its own registry.
            // Both identify the same property by ID, not by descriptor address.
            const auto* registered = incoming.owner.empty() ? m_registry.find(incoming.name)
                                                          : m_registry.find(incoming.owner, incoming.name);
            const auto& d = registered && registered->id == incoming.id ? *registered : incoming;
            if (&d == m_props.Content)
                contentChanged(element);
            if (&d == m_props.DataContext)
            {
                m_bindings->dataContextChanged(element);
                refreshContextual(element);
            }
            if (!m_elementBindings.empty() || !m_accessorBindings.empty())
                elementPropertyChanged(element, d);
            if (!m_tree.contains(element))
                return;
            if (&d == m_props.IsPressed || &d == m_props.IsPointerOver || &d == m_props.IsEnabled)
                updateCommonState(element);
            if (const auto it = m_templateBindings.find(element); it != m_templateBindings.end())
                for (const auto& b : it->second)
                    if (b.ownerProperty == &d)
                        set(b.target, b.property, m_tree.property(element, d), PropertySource::kTemplate);
            if (m_tree.contains(element) && (&d == m_props.IsChecked || &d == m_props.IsOn))
                controlValueChanged(element, &d);
        });
    m_bridge = std::make_unique<VisualBridge>(m_tree, m_compositor, m_visual);
    registerPainters();
    applyWindowBackground();
}

Framework::~Framework()
{
    // Expire non-owning content references before removing UI state.
    m_lifetime.reset();
    // Detach bindings, events and visuals while their framework services still exist.
    while (!m_tree.roots().empty())
        m_tree.remove(m_tree.roots().back());
}

const PropertyDescriptor* Framework::property(std::string_view xamlName) const
{
#define VELORA_LOOKUP_PROPERTY(member, name, kind, choices, value, flags)                                    \
    if (xamlName == name)                                                                                    \
        return m_props.member;
    VELORA_UI_PROPERTIES(VELORA_LOOKUP_PROPERTY)
#undef VELORA_LOOKUP_PROPERTY
    return nullptr;
}

ElementId Framework::create(ElementKind kind, ElementId parent)
{
    // Object first: the element exists, with its type defaults, before the tree hosts it.
    return insert(createElement(kind), parent);
}

ElementId Framework::insert(std::shared_ptr<UIElement> element, ElementId parent)
{
    if (!element || !element->kind() || element->isInTree() || !isOwnerThread())
        return 0;
    const ElementKind kind = *element->kind();
    const ElementId id = m_tree.insert(std::move(element), parent);
    if (id)
        initializeElement(id, kind);
    return id;
}

void Framework::initializeElement(ElementId id, ElementKind kind)
{
    const SchemaType& type = schemaType(kind);
    if (kind == ElementKind::kScrollViewer)
    {
        m_viewOffsets[id] = {0, 0};
        m_tree.onRemoved(id,
                         [this, id]
                         {
                             m_viewOffsets.erase(id);
                             if (m_scrollDrag.viewer == id)
                                 m_scrollDrag = {};
                             if (m_scrollHover == id)
                                 m_scrollHover = 0;
                         });
    }
    if (type.items)
    {
        m_lists[id] = {};
        m_tree.onRemoved(id, [this, id] { m_lists.erase(id); });
    }
}

void Framework::bind(ElementId element, const PropertyDescriptor* property, std::string_view path,
                     BindingMode mode, BindingOptions options)
{
    const auto* schemaProperty = findSchemaProperty(property->name);
    m_bindings->bind(element, *property, schemaProperty ? schemaProperty->kind : ValueKind::kString, path,
                     mode, std::move(options));
}

// ---- element bindings (ElementName, RelativeSource) -------------------------------------------

void Framework::bindElement(ElementId element, const PropertyDescriptor* property, ElementId source,
                            const PropertyDescriptor* sourceProperty, BindingMode mode,
                            BindingOptions options)
{
    if (!property || !sourceProperty || !m_tree.contains(element) || !m_tree.contains(source))
        return;
    auto binding = std::make_shared<ElementBinding>(
        ElementBinding{element, property, source, sourceProperty, mode, std::move(options)});
    pushElementBinding(*binding);
    if (mode == BindingMode::kOneTime)
        return;
    m_elementBindings.push_back(binding);
    const std::weak_ptr<ElementBinding> weak = binding;
    const auto drop = [this, weak]
    {
        if (const auto b = weak.lock())
            std::erase(m_elementBindings, b);
    };
    m_tree.onRemoved(element, drop);
    if (source != element)
        m_tree.onRemoved(source, drop);
}

void Framework::pushElementBinding(ElementBinding& b)
{
    const PropertyValue& value = m_tree.property(b.source, *b.sourceProperty);
    std::optional<PropertyValue> shown =
        b.options.converter ? b.options.converter->convert(value, b.options.converterParameter)
                            : std::optional(value);
    if (!shown)
        shown = b.options.fallback;
    b.writing = true;
    if (shown)
        set(b.target, b.property, std::move(*shown), PropertySource::kLocal);
    else
        clear(b.target, b.property, PropertySource::kLocal);
    b.writing = false;
}

void Framework::bindAccessor(ElementId element, const PropertyDescriptor* property,
                             std::function<std::optional<PropertyValue>()> get,
                             std::function<std::vector<Subscription>(std::function<void()>)> observe,
                             std::function<bool(const PropertyValue&)> set, BindingMode mode, bool contextual)
{
    if (!property || !get || !m_tree.contains(element))
        return;
    auto binding = std::make_shared<AccessorBinding>(
        AccessorBinding{element, property, std::move(get), std::move(observe), std::move(set), mode});
    binding->contextual = contextual;
    pushAccessorBinding(binding);
    if (mode == BindingMode::kOneTime && !contextual)
        return;
    m_accessorBindings.push_back(binding);
    const std::weak_ptr<AccessorBinding> weak = binding;
    m_tree.onRemoved(element,
                     [this, weak]
                     {
                         if (const auto b = weak.lock())
                         {
                             b->subscriptions.clear();
                             std::erase(m_accessorBindings, b);
                         }
                     });
}

// A container got another item (DataContext): its item x:Binds read the new one.
void Framework::refreshContextual(ElementId element)
{
    for (const auto& b : std::vector(m_accessorBindings))
        if (b->contextual)
            for (ElementId e = b->target; e; e = m_tree.parent(e))
                if (e == element)
                {
                    pushAccessorBinding(b);
                    break;
                }
}

void Framework::pushAccessorBinding(const std::shared_ptr<AccessorBinding>& b)
{
    b->writing = true;
    if (auto value = b->get())
        set(b->target, b->property, std::move(*value), PropertySource::kLocal);
    else
        clear(b->target, b->property, PropertySource::kLocal);
    b->writing = false;
    if (b->mode == BindingMode::kOneTime || !b->observe)
        return;
    // Objects along the path may have changed: subscribe again from the start.
    const std::weak_ptr<AccessorBinding> weak = b;
    b->subscriptions = b->observe(
        [this, weak]
        {
            if (const auto binding = weak.lock(); binding && !binding->writing)
                pushAccessorBinding(binding);
        });
}

void Framework::elementPropertyChanged(ElementId element, const PropertyDescriptor& property)
{
    for (const auto& b : std::vector(m_accessorBindings))
        if (b->mode == BindingMode::kTwoWay && !b->writing && b->target == element &&
            b->property == &property && b->set)
        {
            b->writing = true;
            b->set(m_tree.property(element, property));
            b->writing = false;
            // Source notifications are suppressed while writing. Read back even on success:
            // a setter can accept the write but normalize or truncate its value.
            pushAccessorBinding(b);
        }
    // A copy: pushing a value can add or end bindings (a template applied on Content, for example).
    const auto bindings = m_elementBindings;
    for (const auto& b : bindings)
    {
        if (b->writing)
            continue;
        if (b->source == element && b->sourceProperty == &property)
            pushElementBinding(*b);
        else if (b->mode == BindingMode::kTwoWay && b->target == element && b->property == &property)
        {
            PropertyValue value = m_tree.property(b->target, *b->property);
            if (b->options.converter)
            {
                auto back = b->options.converter->convertBack(value, b->options.converterParameter);
                if (!back)
                    continue;
                value = std::move(*back);
            }
            b->writing = true;
            set(b->source, b->sourceProperty, std::move(value), PropertySource::kLocal);
            b->writing = false;
            pushElementBinding(*b); // shows what the source kept (it may coerce)
        }
    }
}

void Framework::on(ElementId element, const EventDescriptor* event, std::function<void()> handler)
{
    m_tree.on(element, event->id,
              [handler = std::move(handler)]
              {
                  handler();
                  return true;
              });
}

double Framework::number(ElementId e, const PropertyDescriptor* d) const
{
    const auto& v = m_tree.property(e, *d);
    if (v.isDouble())
        return v.asDouble();
    if (v.isInt())
        return static_cast<double>(v.asInt());
    return std::numeric_limits<double>::quiet_NaN();
}

Thickness Framework::thickness(ElementId e, const PropertyDescriptor* d) const
{
    const auto& v = m_tree.property(e, *d);
    return v.empty() ? Thickness{} : v.asThickness();
}

std::string_view Framework::text(ElementId e, const PropertyDescriptor* d) const
{
    const auto& v = m_tree.property(e, *d);
    return v.isString() ? std::string_view(v.asString()) : std::string_view();
}
} // namespace velora
