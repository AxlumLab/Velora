#include <algorithm>
#include <utility>
#include <velora/runtime/property/PropertyStore.hpp>

namespace velora
{

namespace
{
template <class It> It lowerBound(It first, It last, u64 property, PropertySource source)
{
    return std::lower_bound(first, last, std::pair(property, source), [](const auto& e, const auto& key)
                            { return std::pair(e.property, e.source) < key; });
}
} // namespace

PropertyStore::~PropertyStore()
{
    for (auto& entry : m_entries)
        if (entry.expression)
            entry.expression->detach();
}

std::vector<PropertyStore::Entry>::iterator PropertyStore::find(u64 property, PropertySource source) noexcept
{
    const auto it = lowerBound(m_entries.begin(), m_entries.end(), property, source);
    return it != m_entries.end() && it->property == property && it->source == source ? it : m_entries.end();
}

std::vector<PropertyStore::Entry>::const_iterator PropertyStore::find(u64 property,
                                                                      PropertySource source) const noexcept
{
    const auto it = lowerBound(m_entries.begin(), m_entries.end(), property, source);
    return it != m_entries.end() && it->property == property && it->source == source ? it : m_entries.end();
}

const PropertyValue* PropertyStore::effectiveBelow(u64 property, PropertySource ceiling) const noexcept
{
    auto it = lowerBound(m_entries.begin(), m_entries.end(), property, ceiling);
    if (it == m_entries.begin())
        return nullptr;
    --it;
    return it->property == property ? &it->value : nullptr;
}

const PropertyValue* PropertyStore::layer(u64 property, PropertySource source) const noexcept
{
    const auto it = find(property, source);
    return it == m_entries.end() ? nullptr : &it->value;
}

std::optional<PropertySource> PropertyStore::source(u64 property) const noexcept
{
    const Entry* result = nullptr;
    for (const auto& entry : m_entries)
    {
        if (entry.property == property)
            result = &entry;
        else if (entry.property > property)
            break;
    }
    return result ? std::optional(result->source) : std::nullopt;
}

PropertyExpression* PropertyStore::expression(u64 property, PropertySource source) const noexcept
{
    const auto it = find(property, source);
    return it == m_entries.end() ? nullptr : it->expression.get();
}

void PropertyStore::set(u64 property, PropertySource source, PropertyValue value,
                        std::shared_ptr<PropertyExpression> expression)
{
    const auto pos = lowerBound(m_entries.begin(), m_entries.end(), property, source);
    if (pos != m_entries.end() && pos->property == property && pos->source == source)
    {
        auto previous = std::exchange(pos->expression, std::move(expression));
        pos->value = std::move(value);
        // Detach last: the expression may call back into the store.
        if (previous && previous.get() != pos->expression.get())
            previous->detach();
        return;
    }
    m_entries.insert(pos, Entry{property, source, std::move(value), std::move(expression)});
}

bool PropertyStore::update(u64 property, PropertySource source, PropertyValue value)
{
    const auto it = find(property, source);
    if (it == m_entries.end())
        return false;
    it->value = std::move(value);
    return true;
}

bool PropertyStore::clear(u64 property, PropertySource source)
{
    const auto it = find(property, source);
    if (it == m_entries.end())
        return false;
    auto expression = std::move(it->expression);
    m_entries.erase(it);
    if (expression)
        expression->detach();
    return true;
}

} // namespace velora
