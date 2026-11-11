// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// Adapted from WinUI 2.3 SwipeControl.cpp, SwipeItems.cpp, SwipeItem.cpp/.idl:
// edge selection, reveal/execute and close/remain-open invocation semantics.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/InputContext.hpp>
#include <velora/fluent/SwipeControl.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kControl = "Velora.Fluent.SwipeControl", kItem = "Velora.Fluent.SwipeItem";
usize sideIndex(SwipeSide side)
{
    const auto index = static_cast<usize>(side);
    if (index >= 4)
        throw std::invalid_argument("Unknown swipe side");
    return index;
}
} // namespace
SwipeItem::SwipeItem() : FluentElement(kItem, "SwipeItem")
{
}
const XamlTypeDeclaration& SwipeItem::declaration()
{
    return fluentDeclaration(kItem);
}
std::string SwipeItem::registerType(Framework& ui)
{
    return registerFluentControl(ui, kItem, [] { return createElement<SwipeItem>(); });
}
std::string SwipeItem::text() const
{
    return getValue(property("Text")).asString();
}
PropertySetResult SwipeItem::setText(std::string value)
{
    return setValue(property("Text"), std::move(value));
}
bool SwipeItem::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == property("BehaviorOnInvoked").id)
        return v.isString() &&
               (v.asString() == "Auto" || v.asString() == "Close" || v.asString() == "RemainOpen");
    return FluentElement::validatePropertyValue(p, v);
}
void SwipeItem::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    bindClick("ActionButton",
              [](FluentElement& base)
              {
                  auto& item = static_cast<SwipeItem&>(base);
                  if (const auto owner = item.m_owner.lock())
                      item.invoke(static_cast<SwipeControl&>(*owner));
              });
    syncParts();
}
void SwipeItem::onDetached() noexcept
{
    m_owner.reset();
    FluentElement::onDetached();
}
void SwipeItem::syncParts()
{
    if (!m_ui || !part("ActionButton"))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_ui->set(part("ActionButton"), m_ui->props().Content, text());
}
void SwipeItem::setCommand(std::function<void()> command)
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeItem belongs to its UI thread");
    m_command = std::move(command);
}
u64 SwipeItem::addInvoked(std::function<void(SwipeControl&)> handler)
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeItem events belong to its UI thread");
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("SwipeItem tokens exhausted");
    const auto token = m_nextToken++;
    m_handlers.emplace(token, std::move(handler));
    return token;
}
bool SwipeItem::removeInvoked(u64 token)
{
    return isOwnerThread() && m_handlers.erase(token) != 0;
}
void SwipeItem::invoke(SwipeControl& control)
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeItem belongs to its UI thread");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    const auto handlers = m_handlers;
    const auto command = m_command;
    const auto behavior = getValue(property("BehaviorOnInvoked")).asString();
    for (const auto& [token, handler] : handlers)
        if (m_handlers.contains(token))
            handler(control);
    if (command)
        command();
    if (behavior != "RemainOpen")
        control.close();
}
SwipeControl::SwipeControl() = default;
std::string SwipeControl::className() const
{
    return std::string(kControl);
}
const XamlTypeDeclaration& SwipeControl::declaration()
{
    return fluentDeclaration(kControl);
}
std::string SwipeControl::registerType(Framework& ui)
{
    return registerFluentControl(ui, kControl, [] { return createElement<SwipeControl>(); });
}
std::string_view SwipeControl::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/SwipeControlView.vxbf";
}
std::string_view SwipeControl::contentHostName() const
{
    return "ContentHost";
}
const SwipeItems& SwipeControl::items(SwipeSide side) const
{
    return m_groups[sideIndex(side)];
}
std::vector<std::shared_ptr<UIElement>> SwipeControl::ownedContentObjects() const
{
    auto result = ContentElement::ownedContentObjects();
    for (const auto& group : m_groups)
        for (const auto& item : group.items)
            result.push_back(item);
    return result;
}
bool SwipeControl::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == elementProperties().props.Content->id && v.isObject() && v.asObject() && v.asObject().type &&
        *v.asObject().type == typeid(UIElement))
    {
        const auto* content = static_cast<UIElement*>(v.asObject().object);
        for (const auto& group : m_groups)
            for (const auto& item : group.items)
                if (content->containsOwnedContent(*item))
                    return false;
    }
    return ContentElement::validatePropertyValue(p, v);
}
void SwipeControl::setItems(SwipeSide side, SwipeItems value)
{
    if (!isOwnerThread() || m_rebuilding)
        throw std::logic_error("Swipe collection is busy or on another thread");
    const auto index = sideIndex(side);
    if (value.mode != SwipeMode::kReveal && value.mode != SwipeMode::kExecute)
        throw std::invalid_argument("Unknown swipe mode");
    if (value.mode == SwipeMode::kExecute && value.items.size() > 1)
        throw std::invalid_argument("Execute supports one SwipeItem");
    std::unordered_set<const SwipeItem*> seen;
    for (usize i = 0; i < 4; ++i)
        for (const auto& item : (i == index ? value : m_groups[i]).items)
        {
            if (!item || !item->isOwnerThread() || !seen.insert(item.get()).second ||
                item->containsOwnedContent(*this) || (content() && content()->containsOwnedContent(*item)))
                throw std::invalid_argument("Invalid or duplicate SwipeItem");
            if (item->isInTree() &&
                (!attachedFramework() ||
                 !attachedFramework()->isWithin(attachedFramework()->tree().element(*item), ownerElement())))
                throw std::invalid_argument("SwipeItem already belongs to another tree");
        }
    m_groups[index] = std::move(value);
    close();
    rebuild();
}
void SwipeControl::onAttached(Framework& ui, ElementId self)
{
    ContentElement::onAttached(ui, self);
    const auto root = ui.tree().parent(contentHost());
    for (usize i = 0; i < 4; ++i)
    {
        m_hosts[i] = ui.create(ElementKind::kStackPanel, root);
        ui.set(m_hosts[i], ui.props().GridRow, i64{i == 2 ? 0 : i == 3 ? 2 : 1});
        ui.set(m_hosts[i], ui.props().GridColumn, i64{i == 0 ? 0 : i == 1 ? 2 : 1});
        ui.set(m_hosts[i], ui.props().Orientation, i < 2 ? "Vertical" : "Horizontal");
        ui.set(m_hosts[i], ui.props().Visibility, false);
    }
    rebuild();
}
void SwipeControl::rebuild()
{
    auto* ui = attachedFramework();
    if (!ui || m_rebuilding)
        return;
    const auto keepAlive = ui->tree().object(ownerElement());
    m_rebuilding = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_rebuilding};
    const auto old = std::exchange(m_nodes, {});
    for (const auto id : old)
        if (attachedFramework() == ui)
            ui->tree().remove(id);
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    for (usize i = 0; i < 4 && attachedFramework() == ui; ++i)
        for (const auto& item : m_groups[i].items)
        {
            const auto id = ui->insert(item, m_hosts[i]);
            if (!id)
                throw std::runtime_error("SwipeItem failed to attach");
            item->m_owner = weak;
            m_nodes.push_back(id);
        }
}
void SwipeControl::open(SwipeSide side)
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeControl belongs to its UI thread");
    const auto index = sideIndex(side);
    if (m_groups[index].items.empty())
        return;
    m_open = side;
    auto* ui = attachedFramework();
    if (!ui)
        return;
    const auto keepAlive = ui->tree().object(ownerElement());
    for (usize i = 0; i < 4 && attachedFramework() == ui; ++i)
        ui->set(m_hosts[i], ui->props().Visibility, i == index);
}
void SwipeControl::close()
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeControl belongs to its UI thread");
    m_open.reset();
    auto* ui = attachedFramework();
    if (!ui)
        return;
    const auto keepAlive = ui->tree().object(ownerElement());
    for (const auto id : m_hosts)
        if (attachedFramework() == ui && id)
            ui->set(id, ui->props().Visibility, false);
}
bool SwipeControl::isOpen() const noexcept
{
    return m_open.has_value();
}
void SwipeControl::invoke(SwipeSide side, usize index)
{
    if (!isOwnerThread())
        throw std::logic_error("SwipeControl belongs to its UI thread");
    const auto& values = items(side).items;
    if (index >= values.size())
        throw std::out_of_range("SwipeItem index");
    const auto item = values[index];
    const auto keepAlive = attachedFramework() ? attachedFramework()->tree().object(ownerElement()) : nullptr;
    item->invoke(*this);
}
void SwipeControl::onDetached() noexcept
{
    m_hosts.fill(0);
    m_nodes.clear();
    m_open.reset();
    m_dragging = false;
    ContentElement::onDetached();
}
bool SwipeControl::isFocusable() const noexcept
{
    return true;
}
void SwipeControl::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    auto* ui = attachedFramework();
    if (!ui || !contentNode() || !ui->isWithin(args.originalSource, contentNode()))
        return;
    if (context.capturePointer())
    {
        m_dragging = true;
        m_startX = args.windowX;
        m_startY = args.windowY;
        context.focus();
        args.handled = true;
    }
}
void SwipeControl::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (m_dragging && !context.hasPointerCapture())
        m_dragging = false;
    if (m_dragging)
        args.handled = true;
}
void SwipeControl::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging || !context.hasPointerCapture())
        return;
    m_dragging = false;
    context.releasePointer();
    args.handled = true;
    const auto x = args.windowX - m_startX, y = args.windowY - m_startY;
    if (std::max(std::abs(x), std::abs(y)) < 24)
    {
        close();
        return;
    }
    const auto side = std::abs(x) >= std::abs(y) ? (x > 0 ? SwipeSide::kLeft : SwipeSide::kRight)
                                                 : (y > 0 ? SwipeSide::kTop : SwipeSide::kBottom);
    open(side);
    if (items(side).mode == SwipeMode::kExecute && !items(side).items.empty() &&
        std::max(std::abs(x), std::abs(y)) >= 80)
        invoke(side, 0);
}
void SwipeControl::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kEscape)
    {
        close();
        args.handled = true;
    }
    else if (args.ctrl)
    {
        if (args.key == Key::kLeft)
            open(SwipeSide::kLeft);
        else if (args.key == Key::kRight)
            open(SwipeSide::kRight);
        else if (args.key == Key::kUp)
            open(SwipeSide::kTop);
        else if (args.key == Key::kDown)
            open(SwipeSide::kBottom);
        else
            return;
        args.handled = true;
    }
}
} // namespace velora
