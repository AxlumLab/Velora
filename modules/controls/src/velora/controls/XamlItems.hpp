#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <velora/data/ObjectRef.hpp>
#include <velora/data/Observable.hpp>
#include <velora/runtime/Framework.hpp>

// ListView items in the .vxbf pipeline (R09.5): an ItemsSource seen without its C++ item type, and
// the host that realizes the visible items through a compiled DataTemplate.
//
// The old pipeline's ItemsRepeater<Item, Template> is a template over the item and the generated
// template class. Compiled XAML knows neither at run time, so here the source is an IItemsSource
// (generated x:Bind code adapts an ObservableVector<T>, where T is known) and each container is an
// instance of the DataTemplate's IR whose DataContext is its item, so {Binding} inside it resolves
// against the item through the reflection accessors. Containers scrolled out of view are pooled and
// rebound, as in ItemsRepeater.

namespace velora
{

class IItemsSource
{
public:
    virtual ~IItemsSource() = default;
    [[nodiscard]] virtual usize size() const = 0;
    [[nodiscard]] virtual ObjectRef at(usize index) const = 0;
    [[nodiscard]] virtual Subscription subscribe(std::function<void(const VectorChange&)> handler) = 0;
};

template <class T> class ObservableVectorSource final : public IItemsSource
{
public:
    explicit ObservableVectorSource(ObservableVector<T>& vector) : m_vector(vector) {}
    usize size() const override
    {
        return m_vector.size();
    }
    ObjectRef at(usize index) const override
    {
        return ObjectRef::borrow(m_vector[index]);
    }
    Subscription subscribe(std::function<void(const VectorChange&)> handler) override
    {
        return m_vector.subscribe(std::move(handler));
    }

private:
    ObservableVector<T>& m_vector; // borrowed: the source outlives the view, as for every x:Bind
};

// An ItemsSource value: an ObjectRef whose type is IItemsSource.
template <class T> PropertyValue itemsSourceValue(ObservableVector<T>& vector)
{
    auto source = std::make_shared<ObservableVectorSource<T>>(vector);
    IItemsSource* erased = source.get();
    return PropertyValue(ObjectRef{erased, &typeid(IItemsSource), std::move(source)});
}
[[nodiscard]] IItemsSource* asItemsSource(const PropertyValue& value) noexcept;

// Creates one container (an instance of the item template) under `parent`; returns its root.
using ItemContainerFactory = std::function<ElementId(ElementId parent)>;

class XamlItemsHost : public std::enable_shared_from_this<XamlItemsHost>
{
public:
    static constexpr usize kMaxPool = 8;

    // The host lives as long as `list`: it is kept by the list's removal callback.
    static std::shared_ptr<XamlItemsHost> attach(Framework& ui, ElementId list);
    // The host attached to `list`, or null.
    static std::shared_ptr<XamlItemsHost> find(const Framework& ui, ElementId list);
    void setFactory(ItemContainerFactory factory);
    void setSource(const PropertyValue& source);

    [[nodiscard]] usize realizedCount() const noexcept
    {
        return m_realized.size();
    }
    [[nodiscard]] usize pooledCount() const noexcept
    {
        return m_pool.size();
    }
    // The container showing item `index`, or 0.
    [[nodiscard]] ElementId container(usize index) const;

private:
    XamlItemsHost(Framework& ui, ElementId list) : m_ui(ui), m_list(list) {}
    struct Slot
    {
        ElementId root;
        usize index;
    };
    void changed(const VectorChange& change);
    void realize();
    void bind(Slot& slot);
    void recycle(usize position);
    void recycleAll();
    void countChanged();

    Framework& m_ui;
    ElementId m_list;
    ItemContainerFactory m_factory;
    PropertyValue m_sourceValue; // keeps the adapter alive
    IItemsSource* m_source = nullptr;
    Subscription m_subscription;
    std::vector<Slot> m_realized;
    std::vector<ElementId> m_pool;
};

} // namespace velora
