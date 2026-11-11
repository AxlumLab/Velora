// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
#include "FluentTypes.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/LayoutContext.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/PaneLayout.hpp>
#include <velora/runtime/SchemaValue.hpp>

namespace velora
{
PaneLayout::PaneLayout() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (!m_ui || p.readOnly)
                return;
            if (p.id == paneProperty(0).id || p.id == paneProperty(1).id)
                syncPanes();
            if (m_ui && (p.id == paneProperty(0).id || p.id == paneProperty(1).id ||
                         p.name.starts_with("SplitView.") || p.name.starts_with("TwoPaneView.")))
                m_ui->tree().invalidateMeasure(m_element);
        });
}
const XamlTypeDeclaration& PaneLayout::paneDeclaration(std::string_view name)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(name);
}
const PropertyDescriptor& PaneLayout::layoutProperty(std::string_view type, std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto* type : {"Velora.Fluent.SplitView", "Velora.Fluent.TwoPaneView"})
            for (const auto& p : paneDeclaration(type).properties)
            {
                const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
                registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                                PropertyFlags::kPaint);
            }
        return true;
    }();
    (void)initialized;
    return *registry.find(paneDeclaration(type).property(name)->runtimeName);
}
std::string PaneLayout::registerLayout(Framework& ui, const XamlTypeDeclaration& type,
                                       std::function<std::shared_ptr<UIElement>()> factory)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : type.properties)
        properties.push_back(&layoutProperty(type.className, p.name));
    return ui.registerSourceControl(type, std::move(factory), properties);
}
Framework* PaneLayout::attachedFramework() const noexcept
{
    return m_ui;
}
ElementId PaneLayout::paneHost(usize index) const noexcept
{
    return m_hosts[index];
}
std::shared_ptr<UIElement> PaneLayout::paneObject(usize index) const
{
    const auto& value = getValue(paneProperty(index));
    if (!value.isObject() || !value.asObject())
        return {};
    const auto& ref = value.asObject();
    return {ref.owner, static_cast<UIElement*>(ref.object)};
}
std::vector<std::shared_ptr<UIElement>> PaneLayout::ownedContentObjects() const
{
    std::vector<std::shared_ptr<UIElement>> result;
    for (usize slot = 0; slot < 2; ++slot)
        if (auto item = paneObject(slot))
            result.push_back(std::move(item));
    return result;
}
PropertySetResult PaneLayout::setPaneObject(usize index, std::shared_ptr<UIElement> value)
{
    return setValue(paneProperty(index), ObjectRef::share<UIElement>(std::move(value)));
}
bool PaneLayout::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    for (usize index = 0; index < 2; ++index)
    {
        if (p.id != paneProperty(index).id)
            continue;
        if (m_syncing)
            return false;
        if (p.id == elementProperties().props.Content->id && value.isString())
            return true;
        if (!value.isObject())
            return false;
        const auto& ref = value.asObject();
        if (!ref)
            return true;
        if (!ref.owner || ref.owner.get() != ref.object || !ref.type || *ref.type != typeid(UIElement))
            return false;
        const auto* item = static_cast<UIElement*>(ref.object);
        if (item == this || !item->isOwnerThread() || item == paneObject(1 - index).get())
            return false;
        if (item->containsOwnedContent(*this))
            return false;
        if (item->isInTree())
            return m_ui && m_ui->tree().element(*item) == m_items[index];
        return true;
    }
    return validateLayoutProperty(p, value);
}
void PaneLayout::syncPanes()
{
    if (!m_ui || m_syncing || !m_hosts[0] || !m_hosts[1])
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_syncing = true;
    struct Reset
    {
        bool& value;
        ~Reset()
        {
            value = false;
        }
    } reset{m_syncing};
    for (usize index = 0; index < 2 && m_ui; ++index)
    {
        const auto desired = paneObject(index);
        const auto value = getValue(paneProperty(index));
        if (desired && m_items[index] && m_ui->tree().object(m_items[index]).get() == desired.get())
            continue;
        if (m_items[index])
        {
            const auto old = std::exchange(m_items[index], 0);
            m_ui->tree().remove(old);
        }
        if (!m_ui)
            return;
        if (desired)
        {
            m_items[index] = m_ui->insert(desired, m_hosts[index]);
            if (!m_items[index])
                throw std::runtime_error("Pane content failed to attach");
        }
        else if (value.isString() && !value.asString().empty())
        {
            m_items[index] = m_ui->create(ElementKind::kTextBlock, m_hosts[index]);
            m_ui->set(m_items[index], m_ui->props().Text, value);
        }
    }
}
void PaneLayout::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/PaneLayoutView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("Pane layout template: " + loaded.error);
    m_root = ui.tree().element(*loaded.root);
    m_hosts = {ui.tree().element(*loaded.names.at("Pane1Host")),
               ui.tree().element(*loaded.names.at("Pane2Host"))};
    m_overlay = ui.tree().element(*loaded.names.at("DismissOverlay"));
    ui.applyTemplate(self, m_root, {{"Pane1Host", m_hosts[0]}, {"Pane2Host", m_hosts[1]}});
    for (const auto id : {m_root, m_hosts[0], m_hosts[1]})
        ui.set(id, elementProperties().visual.clipToBounds, true);
    syncPanes();
}
void PaneLayout::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_root = m_overlay = 0;
    m_hosts = m_items = {};
}
std::array<Size, 2> PaneLayout::measurePanes(LayoutContext& context, Size available)
{
    std::array<Size, 2> result{};
    for (usize index = 0; index < 2; ++index)
        if (m_items[index])
            result[index] = context.measure(m_items[index], available);
    return result;
}
void PaneLayout::applyPlan(const PaneLayoutPlan& plan)
{
    for (usize index = 0; index < 2 && m_ui; ++index)
    {
        if (!plan.visible[index] && m_ui->isWithin(m_ui->focused(), m_hosts[index]))
            m_ui->focus(0);
        if (m_ui && !plan.visible[index] && m_ui->isWithin(m_ui->tree().pointerCapture(), m_hosts[index]))
            m_ui->tree().releasePointer();
        if (m_ui)
            m_ui->set(m_hosts[index], m_ui->props().Visibility, plan.visible[index]);
    }
    if (m_ui)
        m_ui->set(m_overlay, m_ui->props().Visibility, plan.dismissOverlay);
}
Size PaneLayout::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    if (!m_ui)
        return {};
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto plan = layoutPanes(input.content, measurePanes(context, input.content));
    applyPlan(plan);
    for (usize index = 0; index < 2 && m_ui; ++index)
        context.measure(m_hosts[index], {plan.panes[index].width, plan.panes[index].height});
    return {plan.size.width + input.insetX, plan.size.height + input.insetY};
}
void PaneLayout::arrangeOverride(LayoutContext& context, ElementId, Box content,
                                 const std::vector<ElementId>&)
{
    if (!m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const Size available{content.width, content.height};
    const auto plan = layoutPanes(available, measurePanes(context, available));
    applyPlan(plan);
    if (!m_ui)
        return;
    context.arrange(m_root, content);
    context.arrange(m_overlay, {0, 0, content.width, content.height});
    for (usize index = 0; index < 2 && m_ui; ++index)
    {
        context.measure(m_hosts[index], {plan.panes[index].width, plan.panes[index].height});
        context.arrange(m_hosts[index], plan.panes[index]);
    }
    if (m_ui)
        onPlanArranged(plan);
}
} // namespace velora
