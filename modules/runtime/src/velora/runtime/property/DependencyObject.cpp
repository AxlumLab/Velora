#include <algorithm>
#include <utility>
#include <velora/runtime/property/DependencyObject.hpp>

namespace velora
{

DependencyObject::DependencyObject() = default;

DependencyObject::~DependencyObject()
{
    *m_alive = false;
}

std::optional<PropertyValue> DependencyObject::readLocalValue(const PropertyDescriptor& property) const
{
    if (const auto* value = m_store.layer(property.id, PropertySource::kLocal))
        return *value;
    return std::nullopt;
}

const PropertyValue& DependencyObject::getAnimationBaseValue(const PropertyDescriptor& property) const
{
    if (const auto* value = m_store.effectiveBelow(property.id, PropertySource::kAnimation))
        return *value;
    if (property.inherited && m_parent)
        return m_parent->getValue(property);
    return property.defaultValue;
}

std::optional<PropertySource> DependencyObject::valueSource(const PropertyDescriptor& property) const noexcept
{
    return m_store.source(property.id);
}

const PropertyValue* DependencyObject::layerValue(const PropertyDescriptor& property,
                                                  PropertySource source) const noexcept
{
    return m_store.layer(property.id, source);
}

PropertyExpression* DependencyObject::expression(const PropertyDescriptor& property,
                                                 PropertySource source) const noexcept
{
    return m_store.expression(property.id, source);
}

PropertySetResult DependencyObject::setValue(const PropertyDescriptor& property, PropertyValue value,
                                             PropertySource source)
{
    if (property.readOnly)
        return PropertySetResult::kReadOnly;
    return write(property, std::move(value), source);
}

PropertySetResult DependencyObject::setValue(const ReadOnlyPropertyKey& key, PropertyValue value,
                                             PropertySource source)
{
    return write(key.property(), std::move(value), source);
}

bool DependencyObject::clearValue(const PropertyDescriptor& property, PropertySource source)
{
    return !property.readOnly && erase(property, source);
}

bool DependencyObject::clearValue(const ReadOnlyPropertyKey& key, PropertySource source)
{
    return erase(key.property(), source);
}

PropertySetResult DependencyObject::write(const PropertyDescriptor& property, PropertyValue value,
                                          PropertySource source)
{
    if (!isOwnerThread())
        return PropertySetResult::kWrongThread;
    if (property.validate && !value.empty() && !property.validate(value))
        return PropertySetResult::kInvalidValue;
    PropertyValue before = getValue(property);
    PropertyExpression* current = m_store.expression(property.id, source);
    if (current && current->keepWhenOverwritten(value))
        m_store.update(property.id, source, std::move(value)); // e.g. a TwoWay binding stays
    else
        m_store.set(property.id, source, std::move(value)); // replaces (and detaches) the expression
    changed(property, std::move(before));
    return PropertySetResult::kOk;
}

bool DependencyObject::erase(const PropertyDescriptor& property, PropertySource source)
{
    if (!isOwnerThread() || !m_store.layer(property.id, source))
        return false;
    PropertyValue before = getValue(property);
    m_store.clear(property.id, source);
    changed(property, std::move(before));
    return true;
}

PropertySetResult DependencyObject::setExpression(const PropertyDescriptor& property, PropertySource source,
                                                  std::shared_ptr<PropertyExpression> expression,
                                                  PropertyValue initialValue)
{
    if (!isOwnerThread())
        return PropertySetResult::kWrongThread;
    if (property.readOnly)
        return PropertySetResult::kReadOnly;
    if (property.validate && !initialValue.empty() && !property.validate(initialValue))
        return PropertySetResult::kInvalidValue;
    PropertyValue before = getValue(property);
    m_store.set(property.id, source, std::move(initialValue), std::move(expression));
    changed(property, std::move(before));
    return PropertySetResult::kOk;
}

bool DependencyObject::updateExpressionValue(const PropertyDescriptor& property, PropertySource source,
                                             const PropertyExpression& expression, PropertyValue value)
{
    if (!isOwnerThread() || m_store.expression(property.id, source) != &expression)
        return false;
    if (property.validate && !value.empty() && !property.validate(value))
        return false;
    PropertyValue before = getValue(property);
    m_store.update(property.id, source, std::move(value));
    changed(property, std::move(before));
    return true;
}

u64 DependencyObject::addPropertyChanged(ChangedHandler handler)
{
    if (!handler)
        return 0;
    const u64 token = m_nextToken++;
    m_handlers.push_back(std::make_shared<Handler>(Handler{token, std::move(handler)}));
    return token;
}

bool DependencyObject::removePropertyChanged(u64 token)
{
    const auto it = std::find_if(m_handlers.begin(), m_handlers.end(),
                                 [&](const auto& handler) { return handler->token == token; });
    if (it == m_handlers.end())
        return false;
    (*it)->active = false; // a dispatch already holding this handler skips it
    m_handlers.erase(it);
    return true;
}

void DependencyObject::changed(const PropertyDescriptor& property, PropertyValue oldValue)
{
    if (getValue(property) == oldValue)
        return;
    if (m_batchDepth)
    {
        // Coalesce: the first old value of the batch is the one that counts.
        const bool known = std::any_of(m_pending.begin(), m_pending.end(),
                                       [&](const Pending& p) { return p.property == &property; });
        if (!known)
            m_pending.push_back({&property, std::move(oldValue)});
        return;
    }
    notify(property, oldValue);
}

void DependencyObject::notify(const PropertyDescriptor& property, const PropertyValue& oldValue)
{
    if (m_notifyDepth >= kMaxNotificationDepth)
    {
        ++m_dropped;
        return;
    }
    const auto alive = m_alive;
    ++m_notifyDepth;
    const PropertyValue newValue = getValue(property);
    if (m_invalidation)
        m_invalidation->invalidate(*this, property);
    if (*alive && property.changed)
        property.changed(*this, property, oldValue, newValue);
    if (*alive && !m_handlers.empty())
    {
        // A snapshot: handlers added during dispatch wait for the next change, removed ones are
        // skipped, and the object may be destroyed by any of them.
        const auto handlers = m_handlers;
        for (const auto& handler : handlers)
        {
            if (!*alive)
                break;
            if (handler->active)
                handler->callback(*this, property, oldValue, newValue);
        }
    }
    if (*alive)
        --m_notifyDepth;
}

DependencyObject::ChangeBatch::ChangeBatch(DependencyObject& object) noexcept
    : m_object(object), m_alive(object.m_alive)
{
    ++m_object.m_batchDepth;
}

DependencyObject::ChangeBatch::~ChangeBatch()
{
    if (*m_alive)
        m_object.endBatch();
}

void DependencyObject::endBatch()
{
    if (--m_batchDepth)
        return;
    auto pending = std::move(m_pending);
    m_pending.clear();
    const auto alive = m_alive;
    for (auto& entry : pending)
    {
        if (!*alive)
            return;
        if (getValue(*entry.property) != entry.oldValue)
            notify(*entry.property, entry.oldValue);
    }
}

} // namespace velora
