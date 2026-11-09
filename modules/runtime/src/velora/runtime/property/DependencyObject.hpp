#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <velora/runtime/property/PropertyStore.hpp>
#include <velora/runtime/property/PropertyTypes.hpp>

// DependencyObject (R03): the object-level property system.
//
// The object owns its PropertyStore. It works without any UI tree; a tree that hosts the object
// links it to a logical parent for inherited properties and listens for invalidation, but owns
// neither the values nor their precedence.
//
// API (UWP DependencyObject):
//   getValue               effective value: highest layer, else inherited, else metadata default
//   readLocalValue         the Local layer only; nullopt is UWP's UnsetValue
//   getAnimationBaseValue  the effective value with the Animation layer ignored
//   setValue / clearValue  write or remove one layer (Local by default)
//   setExpression          install a binding/resource expression in a layer
//
// Threading: the object belongs to the thread that created it. Writes from other threads return
// PropertySetResult::kWrongThread; reads must also come from the owner thread (they are not
// synchronized), which the hosting runtime guarantees.

namespace velora
{

enum class PropertySetResult : u8
{
    kOk,           // stored; notifications ran if the effective value changed
    kReadOnly,     // the property is read-only and no key was given
    kInvalidValue, // the property's validate callback rejected the value
    kWrongThread
};

// What a layout/render system implements to hear that a property affecting it changed (R03.5).
// The property system only reports the property and its flags; it does not know what measuring,
// arranging or rendering means.
class IPropertyInvalidation
{
public:
    virtual void invalidate(DependencyObject& object, const PropertyDescriptor& property) = 0;

protected:
    ~IPropertyInvalidation() = default;
};

class DependencyObject
{
public:
    using ChangedHandler = std::function<void(DependencyObject&, const PropertyDescriptor&,
                                              const PropertyValue& oldValue, const PropertyValue& newValue)>;

    DependencyObject();
    ~DependencyObject();
    DependencyObject(const DependencyObject&) = delete;
    DependencyObject& operator=(const DependencyObject&) = delete;

    [[nodiscard]] const PropertyValue& getValue(const PropertyDescriptor& property) const
    {
        for (const DependencyObject* object = this; object;
             object = property.inherited ? object->m_parent : nullptr)
            if (const auto* value = object->m_store.effective(property.id))
                return *value;
        return property.defaultValue;
    }
    [[nodiscard]] std::optional<PropertyValue> readLocalValue(const PropertyDescriptor& property) const;
    [[nodiscard]] const PropertyValue& getAnimationBaseValue(const PropertyDescriptor& property) const;
    [[nodiscard]] std::optional<PropertySource>
    valueSource(const PropertyDescriptor& property) const noexcept;
    [[nodiscard]] const PropertyValue* layerValue(const PropertyDescriptor& property,
                                                  PropertySource source) const noexcept;

    PropertySetResult setValue(const PropertyDescriptor& property, PropertyValue value,
                               PropertySource source = PropertySource::kLocal);
    PropertySetResult setValue(const ReadOnlyPropertyKey& key, PropertyValue value,
                               PropertySource source = PropertySource::kLocal);
    // Removes one layer; false when it was not set (or wrong thread). The effective value falls
    // back to the next layer, the inherited value or the default.
    bool clearValue(const PropertyDescriptor& property, PropertySource source = PropertySource::kLocal);
    bool clearValue(const ReadOnlyPropertyKey& key, PropertySource source = PropertySource::kLocal);

    // Installs `expression` in a layer with its first result. A later setValue on that layer
    // replaces it unless PropertyExpression::keepWhenOverwritten says otherwise.
    PropertySetResult setExpression(const PropertyDescriptor& property, PropertySource source,
                                    std::shared_ptr<PropertyExpression> expression,
                                    PropertyValue initialValue);
    // Publishes a new result of the expression installed in `source`; false when that layer does
    // not hold `expression` any more (a stale binding must not overwrite a newer value).
    bool updateExpressionValue(const PropertyDescriptor& property, PropertySource source,
                               const PropertyExpression& expression, PropertyValue value);
    [[nodiscard]] PropertyExpression* expression(const PropertyDescriptor& property,
                                                 PropertySource source) const noexcept;

    // Logical parent for inherited properties (DataContext...). Non-owning: the host clears it
    // before the parent goes away.
    void setInheritanceParent(DependencyObject* parent) noexcept
    {
        m_parent = parent;
    }
    [[nodiscard]] DependencyObject* inheritanceParent() const noexcept
    {
        return m_parent;
    }

    // Instance change handlers; tokens never repeat for this object, 0 is never a token.
    // Removing an unknown token returns false and does nothing (R02.5 token protocol).
    u64 addPropertyChanged(ChangedHandler handler);
    bool removePropertyChanged(u64 token);
    // Non-owning; the sink must outlive the object or be reset first.
    void setInvalidation(IPropertyInvalidation* sink) noexcept
    {
        m_invalidation = sink;
    }

    // Defers notifications of this object until the batch ends; several changes of one property
    // coalesce into a single notification from the first old value to the final value, and a
    // property that ends where it started notifies nothing. Batches nest.
    class ChangeBatch
    {
    public:
        explicit ChangeBatch(DependencyObject& object) noexcept;
        ~ChangeBatch();
        ChangeBatch(const ChangeBatch&) = delete;
        ChangeBatch& operator=(const ChangeBatch&) = delete;

    private:
        DependencyObject& m_object;
        std::shared_ptr<bool> m_alive;
    };

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == m_ownerThread;
    }
    [[nodiscard]] const PropertyStore& store() const noexcept
    {
        return m_store;
    }
    // Notifications refused because they nested deeper than kMaxNotificationDepth (a change
    // handler that keeps changing what it observes). Diagnostic only.
    [[nodiscard]] u64 droppedNotifications() const noexcept
    {
        return m_dropped;
    }

    static constexpr u32 kMaxNotificationDepth = 32;

private:
    struct Handler
    {
        u64 token;
        ChangedHandler callback;
        bool active = true;
    };
    struct Pending
    {
        const PropertyDescriptor* property;
        PropertyValue oldValue;
    };

    PropertySetResult write(const PropertyDescriptor& property, PropertyValue value, PropertySource source);
    bool erase(const PropertyDescriptor& property, PropertySource source);
    // Called after a layer changed with the effective value from before; notifies when it differs.
    void changed(const PropertyDescriptor& property, PropertyValue oldValue);
    void notify(const PropertyDescriptor& property, const PropertyValue& oldValue);
    void endBatch();

    PropertyStore m_store;
    DependencyObject* m_parent = nullptr;
    IPropertyInvalidation* m_invalidation = nullptr;
    std::vector<std::shared_ptr<Handler>> m_handlers;
    u64 m_nextToken = 1;
    u32 m_batchDepth = 0;
    u32 m_notifyDepth = 0;
    u64 m_dropped = 0;
    std::vector<Pending> m_pending;
    // Expires with the object, so a handler that destroys it does not make the dispatch loop
    // touch freed memory.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::thread::id m_ownerThread = std::this_thread::get_id();
};

} // namespace velora
