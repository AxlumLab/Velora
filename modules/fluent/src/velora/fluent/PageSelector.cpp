// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System Pivot/FlipView behavior implemented by Velora; theme references: porting/Pivot/, porting/FlipView/.
#include "FluentTypes.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/FlipView.hpp>
#include <velora/fluent/Pivot.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
namespace
{
struct Busy
{
    bool& flag;
    explicit Busy(bool& value) : flag(value)
    {
        flag = true;
    }
    ~Busy()
    {
        flag = false;
    }
};
const PropertyDescriptor& descriptor(const XamlTypeDeclaration& type, std::string_view name)
{
    static PropertyRegistry registry;
    const auto* p = type.property(name);
    if (const auto* found = registry.find(p->runtimeName))
        return *found;
    const SchemaProperty schema{p->name, p->kind, {}, p->defaultValue, kSchemaPaint};
    return registry.define(p->runtimeName, std::get<PropertyValue>(parseValue(schema, p->defaultValue)),
                           PropertyFlags::kPaint);
}
} // namespace
const XamlTypeDeclaration& PageSelector::pageDeclaration(std::string_view type)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(type);
}
const PropertyDescriptor& PageSelector::property(std::string_view name) const
{
    return descriptor(pageDeclaration(className()), name);
}
std::string PageSelector::registerSelector(Framework& ui, const XamlTypeDeclaration& type,
                                           SourceControlRegistry::Factory factory)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : type.properties)
        properties.push_back(&descriptor(type, p.name));
    return ui.registerSourceControl(type, std::move(factory), properties);
}
PageSelector::PageSelector(bool pivot) : UIElement(ElementKind::kUserControl), m_pivot(pivot)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&, const PropertyValue&)
        {
            if (m_ui && !m_busy)
                updateSelection();
        });
}
const PropertyDescriptor& PageSelector::selectedIndexProperty() const
{
    return property("SelectedIndex");
}
i32 PageSelector::selectedIndex() const
{
    return static_cast<i32>(getValue(selectedIndexProperty()).asInt());
}
PropertySetResult PageSelector::setSelectedIndex(i32 index)
{
    return setValue(selectedIndexProperty(), static_cast<i64>(index));
}
std::shared_ptr<UIElement> PageSelector::selectedItem() const
{
    const auto index = selectedIndex();
    return index >= 0 && static_cast<usize>(index) < m_pages.size() ? m_pages[index].content : nullptr;
}
const std::vector<SelectorPage>& PageSelector::pages() const noexcept
{
    return m_pages;
}
std::vector<std::shared_ptr<UIElement>> PageSelector::ownedContentObjects() const
{
    std::vector<std::shared_ptr<UIElement>> result;
    result.reserve(m_pages.size());
    for (const auto& page : m_pages)
        result.push_back(page.content);
    return result;
}
void PageSelector::setPages(std::vector<SelectorPage> pages)
{
    if (!isOwnerThread() || m_busy)
        throw std::logic_error("Page collection is busy or belongs to another thread");
    if (pages.size() > static_cast<usize>(std::numeric_limits<i32>::max()))
        throw std::length_error("Too many pages");
    std::unordered_set<const UIElement*> unique;
    for (const auto& page : pages)
    {
        utf8ToUtf16(page.header);
        if (!page.content || !page.content->isOwnerThread() || page.content->containsOwnedContent(*this) ||
            !unique.insert(page.content.get()).second)
            throw std::invalid_argument("Pages require distinct UI-thread content without ownership cycles");
        const auto existing = std::ranges::find_if(m_pages, [&](const SelectorPage& old)
                                                   { return old.content == page.content; });
        if (page.content->isInTree() && (existing == m_pages.end() || !m_ui ||
                                         !m_ui->isWithin(m_ui->tree().element(*page.content), m_element)))
            throw std::invalid_argument("Page content is already hosted elsewhere");
    }
    for (usize index = 0; index < pages.size(); ++index)
        for (usize other = 0; other < pages.size(); ++other)
            if (index != other && pages[index].content->containsOwnedContent(*pages[other].content))
                throw std::invalid_argument("A page cannot own another entry in the same collection");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    {
        Busy busy(m_busy);
        // Keep old objects alive until their actual tree nodes have been removed.
        auto previous = std::exchange(m_pages, std::move(pages));
        if (m_ui)
        {
            const auto hosts = std::exchange(m_hosts, {});
            for (const auto host : hosts)
                if (m_ui)
                    m_ui->tree().remove(host);
        }
        if (m_pages.empty())
            setSelectedIndex(-1);
        else if (selectedIndex() < 0 || selectedIndex() >= static_cast<i32>(m_pages.size()))
            setSelectedIndex(0);
    }
    rebuild();
}
u64 PageSelector::addSelectionChanged(std::function<void(i32, i32)> handler)
{
    if (!handler)
        return 0;
    const auto id = selectedIndexProperty().id;
    return addPropertyChanged(
        [id, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                           const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == id)
                handler(static_cast<i32>(before.asInt()), static_cast<i32>(after.asInt()));
        });
}
bool PageSelector::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == selectedIndexProperty().id)
        return value.isInt() && value.asInt() >= -1 && value.asInt() <= std::numeric_limits<i32>::max();
    if (!m_pivot)
        return true;
    if (p.id == property("IsLocked").id)
        return value.isBool();
    if (p.id == property("Title").id)
    {
        if (!value.isString())
            return false;
        try
        {
            utf8ToUtf16(value.asString());
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return true;
}
void PageSelector::normalizePropertyValues(const PropertyDescriptor& p)
{
    if (p.id == selectedIndexProperty().id && !m_pages.empty() &&
        selectedIndex() >= static_cast<i32>(m_pages.size()))
        normalizeValue(selectedIndexProperty(), i64{-1});
}
bool PageSelector::isLocked() const
{
    return m_pivot && getValue(property("IsLocked")).asBool();
}
void PageSelector::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/PageSelectorView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("Page selector template: " + loaded.error);
    m_headerHost = ui.tree().element(*loaded.names.at("Headers"));
    m_contentHost = ui.tree().element(*loaded.names.at("PageHost"));
    m_title = ui.tree().element(*loaded.names.at("TitleText"));
    m_previous = ui.tree().element(*loaded.names.at("PreviousButton"));
    m_next = ui.tree().element(*loaded.names.at("NextButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
    ui.set(m_contentHost, elementProperties().visual.clipToBounds, true);
    rebuild();
}
void PageSelector::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_headerHost = m_contentHost = m_title = m_previous = m_next = 0;
    m_hosts.clear();
    m_headers.clear();
    m_events.clear();
}
void PageSelector::rebuild()
{
    if (!m_ui || m_busy)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    {
        Busy busy(m_busy);
        m_events.clear();
        m_headers.clear();
        m_hosts.clear();
        for (const auto parent : {m_headerHost, m_contentHost})
        {
            if (!m_ui)
                return;
            const auto old = m_ui->tree().children(parent);
            for (const auto id : old)
                if (m_ui)
                    m_ui->tree().remove(id);
        }
        const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
        try
        {
            for (usize index = 0; m_ui && index < m_pages.size(); ++index)
            {
                const auto host = m_ui->create(ElementKind::kBorder, m_contentHost);
                if (!m_ui)
                    return;
                m_hosts.push_back(host);
                if (!m_ui->insert(m_pages[index].content, host))
                    throw std::runtime_error("Page content failed to attach");
                if (!m_ui)
                    return;
                if (m_pivot)
                {
                    const auto header = m_ui->create(ElementKind::kButton, m_headerHost);
                    if (!m_ui)
                        return;
                    m_headers.push_back(header);
                    m_ui->set(header, m_ui->props().Content, m_pages[index].header);
                    if (!m_ui)
                        return;
                    m_events.push_back(m_ui->subscribeEvent(
                        header, m_ui->events().Click,
                        [weak, index](ElementId, RoutedEventArgs&)
                        {
                            if (const auto owner = weak.lock())
                                static_cast<PageSelector&>(*owner).navigate(static_cast<i32>(index), true);
                        }));
                }
            }
            for (const auto previous : {true, false})
            {
                if (!m_ui)
                    return;
                m_events.push_back(m_ui->subscribeEvent(
                    previous ? m_previous : m_next, m_ui->events().Click,
                    [weak, previous](ElementId, RoutedEventArgs&)
                    {
                        if (const auto owner = weak.lock())
                        {
                            auto& control = static_cast<PageSelector&>(*owner);
                            control.navigate(control.selectedIndex() + (previous ? -1 : 1), false);
                        }
                    }));
            }
        }
        catch (...)
        {
            m_events.clear();
            const auto hosts = std::exchange(m_hosts, {});
            for (const auto id : hosts)
                if (m_ui)
                    m_ui->tree().remove(id);
            const auto headers = std::exchange(m_headers, {});
            for (const auto id : headers)
                if (m_ui)
                    m_ui->tree().remove(id);
            throw;
        }
    }
    updateSelection();
}
void PageSelector::updateSelection()
{
    if (!m_ui || m_busy)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    Busy busy(m_busy);
    const auto& props = elementProperties().props;
    const auto set = [this](ElementId id, const PropertyDescriptor* property, PropertyValue value)
    {
        if (m_ui)
            m_ui->set(id, property, std::move(value));
    };
    set(m_title, props.Text, m_pivot ? getValue(property("Title")).asString() : "");
    set(m_title, props.Visibility, m_pivot && !getValue(property("Title")).asString().empty());
    set(m_headerHost, props.Visibility, m_pivot);
    set(m_previous, props.Visibility, !m_pivot);
    set(m_next, props.Visibility, !m_pivot);
    set(m_previous, props.IsEnabled, selectedIndex() > 0);
    set(m_next, props.IsEnabled,
        selectedIndex() >= 0 && static_cast<usize>(selectedIndex()) + 1 < m_pages.size());
    const auto hosts = m_hosts;
    for (usize index = 0; m_ui && index < hosts.size(); ++index)
    {
        const bool selected = static_cast<i32>(index) == selectedIndex();
        if (!selected)
        {
            if (m_ui->isWithin(m_ui->focused(), hosts[index]))
                m_ui->focus(m_pivot && selectedIndex() >= 0 &&
                                    static_cast<usize>(selectedIndex()) < m_headers.size()
                                ? m_headers[selectedIndex()]
                                : m_element);
            if (m_ui && m_ui->isWithin(m_ui->tree().pointerCapture(), hosts[index]))
                m_ui->tree().releasePointer();
        }
        set(hosts[index], props.Visibility, selected);
        if (m_pivot && index < m_headers.size())
        {
            const auto header = m_headers[index];
            set(header, props.FontSize, selected ? 20.0 : 16.0);
            set(header, props.IsTabStop, selected || (selectedIndex() < 0 && index == 0));
            set(header, props.IsEnabled, !isLocked() || selected);
        }
    }
}
void PageSelector::navigate(i32 index, bool focusHeader)
{
    if (!m_ui || m_busy || isLocked() || index < 0 || static_cast<usize>(index) >= m_pages.size())
    {
        updateSelection();
        return;
    }
    const auto keepAlive = m_ui->tree().object(m_element);
    setSelectedIndex(index);
    updateSelection();
    if (m_ui && !m_pivot)
        m_ui->focus(m_element);
    else if (m_ui && focusHeader && static_cast<usize>(index) < m_headers.size())
        m_ui->focus(m_headers[index]);
}
void PageSelector::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui || args.alt || m_pages.empty())
        return;
    if (m_pivot && args.ctrl && args.key == Key::kTab)
    {
        const auto count = static_cast<i32>(m_pages.size());
        const auto base = selectedIndex() < 0 ? (args.shift ? 0 : -1) : selectedIndex();
        navigate(static_cast<i32>((static_cast<i64>(base) + (args.shift ? count - 1 : 1)) % count), true);
        args.handled = true;
        return;
    }
    if (args.ctrl)
        return;
    const auto focus = m_ui->focused();
    if (m_pivot ? !m_ui->isWithin(focus, m_headerHost)
                : focus != m_element && focus != m_previous && focus != m_next)
        return;
    i32 index = selectedIndex();
    if (args.key == Key::kHome)
        index = 0;
    else if (args.key == Key::kEnd)
        index = static_cast<i32>(m_pages.size()) - 1;
    else if (args.key == Key::kRight || (!m_pivot && args.key == Key::kDown))
        ++index;
    else if (args.key == Key::kLeft || (!m_pivot && args.key == Key::kUp))
        --index;
    else
        return;
    navigate(std::clamp(index, 0, static_cast<i32>(m_pages.size()) - 1), m_pivot);
    args.handled = true;
}
bool PageSelector::isFocusable() const noexcept
{
    return !m_pivot;
}
std::string PageSelector::automationName() const
{
    const auto index = selectedIndex();
    return index >= 0 && static_cast<usize>(index) < m_pages.size() ? m_pages[index].header : std::string{};
}
Pivot::Pivot() : PageSelector(true)
{
}
FlipView::FlipView() : PageSelector(false)
{
}
#define VELORA_PAGE_SELECTOR(Type)                                                                           \
    std::string Type::className() const                                                                      \
    {                                                                                                        \
        return "Velora.Fluent." #Type;                                                                       \
    }                                                                                                        \
    const XamlTypeDeclaration& Type::declaration()                                                           \
    {                                                                                                        \
        return pageDeclaration("Velora.Fluent." #Type);                                                      \
    }                                                                                                        \
    std::string Type::registerType(Framework& ui)                                                            \
    {                                                                                                        \
        return registerSelector(ui, declaration(), [] { return createElement<Type>(); });                    \
    }
VELORA_PAGE_SELECTOR(Pivot)
VELORA_PAGE_SELECTOR(FlipView)
#undef VELORA_PAGE_SELECTOR
std::string Pivot::title() const
{
    return getValue(property("Title")).asString();
}
PropertySetResult Pivot::setTitle(std::string value)
{
    return setValue(property("Title"), std::move(value));
}
bool Pivot::isLocked() const
{
    return getValue(property("IsLocked")).asBool();
}
PropertySetResult Pivot::setIsLocked(bool value)
{
    return setValue(property("IsLocked"), value);
}
} // namespace velora
