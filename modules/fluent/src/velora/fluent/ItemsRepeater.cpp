// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// Fixed-extent StackLayout realization-window and recycling slice adapted from
// WinUI 2.3 Repeater/StackLayout.cpp, ViewManager.cpp and ElementManager.cpp.
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/LayoutContext.hpp>
#include <velora/fluent/ItemsRepeater.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kType = "Velora.Fluent.ItemsRepeater";
}
ItemsRepeater::ItemsRepeater() : FluentElement(kType, "ItemsRepeater")
{
}
const XamlTypeDeclaration& ItemsRepeater::declaration()
{
    return fluentDeclaration(kType);
}
std::string ItemsRepeater::registerType(Framework& ui)
{
    return registerFluentControl(ui, kType, [] { return createElement<ItemsRepeater>(); });
}
bool ItemsRepeater::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == property("ItemExtent").id)
        return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() > 0 && v.asDouble() <= 1000000;
    if (p.id == property("Spacing").id)
        return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() >= 0 && v.asDouble() <= 1000000;
    if (p.id == property("CacheLength").id)
        return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() >= 0 && v.asDouble() <= 10;
    return FluentElement::validatePropertyValue(p, v);
}
f64 ItemsRepeater::stride() const
{
    return getValue(property("ItemExtent")).asDouble() + getValue(property("Spacing")).asDouble();
}
void ItemsRepeater::setItems(std::vector<std::string> values)
{
    const auto data = std::make_shared<const std::vector<std::string>>(std::move(values));
    if (data->size() > 1000000)
        throw std::invalid_argument("Repeater source exceeds current limit");
    setItemsSource(static_cast<i32>(data->size()),
                   [data](i32 index, std::shared_ptr<UIElement> recycled)
                   {
                       auto element = recycled && recycled->is(ElementKind::kTextBlock)
                                          ? std::move(recycled)
                                          : createElement(ElementKind::kTextBlock);
                       element->setValue(*elementProperties().props.Text, (*data)[index]);
                       return element;
                   });
}
void ItemsRepeater::setItemsSource(i32 count, ElementFactory factory)
{
    if (!isOwnerThread() || m_realizing)
        throw std::logic_error("Repeater source is busy or on another thread");
    if (count < 0 || count > 1000000 || (count > 0 && !factory))
        throw std::invalid_argument("Invalid repeater source");
    {
        m_realizing = true;
        struct Reset
        {
            bool& flag;
            ~Reset()
            {
                flag = false;
            }
        } reset{m_realizing};
        clearRealized(false);
        m_pool.clear();
        m_count = count;
        m_factory = std::move(factory);
    }
    syncParts();
}
i32 ItemsRepeater::itemCount() const noexcept
{
    return m_count;
}
usize ItemsRepeater::realizedCount() const noexcept
{
    return m_realized.size();
}
std::shared_ptr<UIElement> ItemsRepeater::tryGetElement(i32 index) const
{
    const auto found = m_realized.find(index);
    return m_ui && found != m_realized.end()
               ? std::dynamic_pointer_cast<UIElement>(m_ui->tree().object(found->second))
               : nullptr;
}
i32 ItemsRepeater::getElementIndex(const UIElement& element) const
{
    if (m_ui)
        for (const auto& [index, node] : m_realized)
            if (m_ui->tree().uiElement(node) == &element)
                return index;
    return -1;
}
std::shared_ptr<UIElement> ItemsRepeater::createAt(i32 index)
{
    if (!m_ui || index < 0 || index >= m_count || !m_factory)
        return {};
    if (const auto existing = tryGetElement(index))
        return existing;
    std::shared_ptr<UIElement> recycled;
    if (!m_pool.empty())
    {
        recycled = std::move(m_pool.back());
        m_pool.pop_back();
    }
    const auto element = m_factory(index, std::move(recycled));
    if (!element || !element->isOwnerThread() || element->isInTree() || element->containsOwnedContent(*this))
        throw std::invalid_argument("Repeater factory returned invalid content");
    if (!m_ui)
        return {};
    element->setValue(*m_ui->props().Height, getValue(property("ItemExtent")));
    element->setValue(*m_ui->props().CanvasTop, index * stride());
    const auto node = m_ui->insert(element, m_root);
    if (!node)
        throw std::runtime_error("Repeater element failed to attach");
    m_realized.emplace(index, node);
    return element;
}
std::shared_ptr<UIElement> ItemsRepeater::getOrCreateElement(i32 index)
{
    if (!isOwnerThread() || m_realizing)
        throw std::logic_error("Repeater realization is busy or on another thread");
    if (index < 0 || index >= m_count)
        throw std::out_of_range("Repeater index");
    m_realizing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_realizing};
    return createAt(index);
}
void ItemsRepeater::clearRealized(bool recycle)
{
    const auto old = std::exchange(m_realized, {});
    for (const auto& [index, node] : old)
    {
        (void)index;
        if (!m_ui)
            break;
        const auto element = std::dynamic_pointer_cast<UIElement>(m_ui->tree().object(node));
        m_ui->tree().remove(node);
        if (recycle && element && m_pool.size() < 128)
            m_pool.push_back(element);
    }
}
void ItemsRepeater::realize()
{
    if (!m_ui || !m_root || m_realizing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* ui = m_ui;
    m_realizing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_realizing};
    const auto extent = std::max(0.0, m_count * stride() - getValue(property("Spacing")).asDouble());
    const auto offset = std::clamp(m_offset, 0.0, std::max(0.0, extent - m_viewport));
    const auto cache = m_viewport * getValue(property("CacheLength")).asDouble();
    const auto first =
        static_cast<i32>(std::clamp(std::floor((offset - cache) / stride()), 0.0, static_cast<f64>(m_count)));
    const auto last = static_cast<i32>(std::clamp(
        std::ceil((offset + std::max(1.0, m_viewport) + cache) / stride()), 0.0, static_cast<f64>(m_count)));
    const auto old = m_realized;
    for (const auto& [index, node] : old)
        if (index < first || index >= last)
        {
            if (m_ui != ui)
                return;
            const auto element = std::dynamic_pointer_cast<UIElement>(ui->tree().object(node));
            m_realized.erase(index);
            ui->tree().remove(node);
            if (element && m_pool.size() < 128)
                m_pool.push_back(element);
        }
    for (i32 i = first; i < last && m_ui == ui; ++i)
        createAt(i);
    for (const auto& [index, node] : m_realized)
    {
        if (m_ui != ui)
            return;
        ui->set(node, ui->props().Height, getValue(property("ItemExtent")));
        if (m_ui != ui)
            return;
        ui->set(node, ui->props().CanvasTop, index * stride());
    }
}
void ItemsRepeater::syncParts()
{
    if (m_ui && !m_realizing)
    {
        realize();
        if (m_ui)
            m_ui->tree().invalidateMeasure(m_element);
    }
}
void ItemsRepeater::setRealizationWindow(f64 offset, f64 height)
{
    if (!isOwnerThread())
        throw std::logic_error("Repeater belongs to its UI thread");
    if (!std::isfinite(offset) || !std::isfinite(height) || offset < 0 || height < 0)
        throw std::invalid_argument("Invalid realization window");
    if (m_offset == offset && m_viewport == height)
        return;
    m_offset = offset;
    m_viewport = height;
    syncParts();
}
std::vector<std::shared_ptr<UIElement>> ItemsRepeater::ownedContentObjects() const
{
    auto result = m_pool;
    for (const auto& [index, node] : m_realized)
    {
        (void)node;
        if (const auto item = tryGetElement(index))
            result.push_back(item);
    }
    return result;
}
void ItemsRepeater::updateWindow()
{
    const auto source = m_scrollSource.lock();
    if (!source)
        return;
    const auto& p = elementProperties().props;
    const auto height = source->getValue(*p.ViewportHeight).asDouble();
    setRealizationWindow(source->getValue(*p.VerticalOffset).asDouble(), height > 0 ? height : 240);
}
void ItemsRepeater::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    for (auto parent = ui.tree().parent(self); parent; parent = ui.tree().parent(parent))
        if (ui.tree().is(parent, ElementKind::kScrollViewer))
        {
            const auto source = std::dynamic_pointer_cast<UIElement>(ui.tree().object(parent));
            m_scrollSource = source;
            m_viewportToken = source->addPropertyChanged(
                [weak](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&,
                       const PropertyValue&)
                {
                    if (p.id == elementProperties().props.ViewportHeight->id)
                        if (const auto owner = weak.lock())
                            static_cast<ItemsRepeater&>(*owner).updateWindow();
                });
            m_viewChanged = ui.subscribeEvent(parent, ui.events().ViewChanged,
                                              [weak](ElementId, RoutedEventArgs&)
                                              {
                                                  if (const auto owner = weak.lock())
                                                      static_cast<ItemsRepeater&>(*owner).updateWindow();
                                              });
            break;
        }
    updateWindow();
    syncParts();
}
void ItemsRepeater::onDetached() noexcept
{
    if (const auto source = m_scrollSource.lock())
        source->removePropertyChanged(m_viewportToken);
    m_scrollSource.reset();
    m_viewportToken = 0;
    m_viewChanged.reset();
    m_realized.clear();
    m_pool.clear();
    FluentElement::onDetached();
}
Size ItemsRepeater::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    realize();
    if (m_root)
        context.measure(m_root, {input.content.width, std::numeric_limits<f32>::infinity()});
    return {std::isfinite(input.content.width) ? input.content.width : 0,
            static_cast<f32>(std::max(0.0, m_count * stride() - getValue(property("Spacing")).asDouble()))};
}
} // namespace velora
