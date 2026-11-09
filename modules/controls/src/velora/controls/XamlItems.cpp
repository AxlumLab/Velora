#include <algorithm>
#include <cmath>
#include <map>
#include <velora/controls/XamlItems.hpp>

namespace velora
{

IItemsSource* asItemsSource(const PropertyValue& value) noexcept
{
    if (!value.isObject())
        return nullptr;
    const ObjectRef& ref = value.asObject();
    return ref.type && *ref.type == typeid(IItemsSource) ? static_cast<IItemsSource*>(ref.object) : nullptr;
}

namespace
{
// Every attached host, by framework and list; an entry goes with its list.
std::map<std::pair<const Framework*, ElementId>, std::weak_ptr<XamlItemsHost>>& hosts()
{
    static thread_local std::map<std::pair<const Framework*, ElementId>, std::weak_ptr<XamlItemsHost>>
        s_hosts;
    return s_hosts;
}
} // namespace

std::shared_ptr<XamlItemsHost> XamlItemsHost::find(const Framework& ui, ElementId list)
{
    const auto it = hosts().find({&ui, list});
    return it == hosts().end() ? nullptr : it->second.lock();
}

std::shared_ptr<XamlItemsHost> XamlItemsHost::attach(Framework& ui, ElementId list)
{
    if (auto existing = find(ui, list))
        return existing;
    std::shared_ptr<XamlItemsHost> host(new XamlItemsHost(ui, list));
    hosts()[{&ui, list}] = host;
    ui.tree().onRemoved(list,
                        [key = std::pair<const Framework*, ElementId>(&ui, list)] { hosts().erase(key); });
    const std::weak_ptr<XamlItemsHost> weak = host;
    ui.onViewport(list,
                  [weak]
                  {
                      if (const auto h = weak.lock())
                          h->realize();
                  });
    // The removal callback owns the host; it does not touch the tree when it goes.
    ui.tree().onRemoved(list,
                        [host]
                        {
                            host->m_subscription = {};
                            host->m_source = nullptr;
                            host->m_sourceValue = {};
                            host->m_factory = {};
                            host->m_realized.clear();
                            host->m_pool.clear();
                        });
    return host;
}

void XamlItemsHost::setFactory(ItemContainerFactory factory)
{
    recycleAll();
    for (const ElementId root : m_pool)
        m_ui.tree().remove(root);
    m_pool.clear();
    m_factory = std::move(factory);
    realize();
}

void XamlItemsHost::setSource(const PropertyValue& source)
{
    recycleAll();
    m_sourceValue = source;
    m_source = asItemsSource(source);
    const std::weak_ptr<XamlItemsHost> weak = weak_from_this();
    m_subscription = m_source ? m_source->subscribe(
                                    [weak](const VectorChange& c)
                                    {
                                        if (const auto h = weak.lock())
                                            h->changed(c);
                                    })
                              : Subscription{};
    countChanged();
    realize();
}

ElementId XamlItemsHost::container(usize index) const
{
    for (const auto& slot : m_realized)
        if (slot.index == index)
            return slot.root;
    return 0;
}

void XamlItemsHost::changed(const VectorChange& c)
{
    using Kind = VectorChange::Kind;
    if (c.kind == Kind::kReset)
        recycleAll();
    else
        for (usize i = 0; i < m_realized.size();)
        {
            auto& slot = m_realized[i];
            if (c.kind == Kind::kRemoved && slot.index == c.index)
            {
                recycle(i);
                continue;
            }
            if (c.kind == Kind::kReplaced && slot.index == c.index)
                bind(slot);
            if (c.kind == Kind::kInserted && slot.index >= c.index)
                ++slot.index, bind(slot);
            if (c.kind == Kind::kRemoved && slot.index > c.index)
                --slot.index, bind(slot);
            ++i;
        }
    countChanged();
    realize();
}

void XamlItemsHost::countChanged()
{
    m_ui.set(m_list, m_ui.props().ItemCount,
             PropertyValue(static_cast<i64>(m_source ? m_source->size() : 0)));
}

void XamlItemsHost::realize()
{
    if (!m_factory)
        return;
    const auto v = m_ui.viewport(m_list);
    usize first = 0, last = 0;
    if (m_source && v.itemHeight > 0 && v.height > 0)
    {
        first = static_cast<usize>(std::floor(v.offset / v.itemHeight));
        last =
            std::min(m_source->size(), static_cast<usize>(std::ceil((v.offset + v.height) / v.itemHeight)));
    }
    for (usize i = 0; i < m_realized.size();)
        if (m_realized[i].index < first || m_realized[i].index >= last)
            recycle(i);
        else
            ++i;
    for (usize index = first; index < last; ++index)
    {
        if (container(index))
            continue;
        ElementId root = 0;
        if (!m_pool.empty())
        {
            root = m_pool.back();
            m_pool.pop_back();
        }
        else if (!(root = m_factory(m_list)))
            return; // the template failed; nothing to show
        m_ui.set(root, m_ui.props().Visibility, PropertyValue(true));
        m_realized.push_back({root, index});
        bind(m_realized.back());
    }
    while (m_pool.size() > kMaxPool)
    {
        m_ui.tree().remove(m_pool.back());
        m_pool.pop_back();
    }
}

void XamlItemsHost::bind(Slot& slot)
{
    m_ui.set(slot.root, m_ui.props().ItemIndex, PropertyValue(static_cast<i64>(slot.index)));
    m_ui.set(slot.root, m_ui.props().DataContext, PropertyValue(m_source->at(slot.index)));
}

void XamlItemsHost::recycle(usize position)
{
    const ElementId root = m_realized[position].root;
    m_realized.erase(m_realized.begin() + static_cast<std::ptrdiff_t>(position));
    // No DataContext: the container's bindings drop their subscriptions to the item.
    m_ui.clear(root, m_ui.props().DataContext);
    m_ui.set(root, m_ui.props().Visibility, PropertyValue(false));
    m_pool.push_back(root);
}

void XamlItemsHost::recycleAll()
{
    while (!m_realized.empty())
        recycle(m_realized.size() - 1);
}

} // namespace velora
