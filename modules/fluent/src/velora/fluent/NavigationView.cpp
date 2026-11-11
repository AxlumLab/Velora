// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 NavigationView.cpp/.idl: adaptive mode thresholds, pane and item invocation.
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <velora/controls/LayoutContext.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/NavigationView.hpp>
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
const PropertyDescriptor& internalPane()
{
    static PropertyRegistry r;
    static const auto& p = r.define("NavigationView.InternalPane", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
const ReadOnlyPropertyKey& modeKey()
{
    static PropertyRegistry r;
    static const auto key = []
    {
        PropertyDescriptor p;
        p.name = "NavigationView.DisplayMode";
        p.defaultValue = "Minimal";
        p.flags = PropertyFlags::kPaint;
        return r.defineReadOnly(std::move(p));
    }();
    return key;
}
} // namespace
NavigationView::NavigationView()
{
    m_settings = createElement<NavigationViewItem>();
    m_settings->setContent("Settings");
    m_settings->setIconGlyph("\xEE\x9C\x93");
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == property("IsPaneOpen").id && !m_adapting)
                m_forceClosed = !isPaneOpen();
            if (attachedFramework() && !m_busy)
                updateParts();
        });
}
std::string NavigationView::className() const
{
    return "Velora.Fluent.NavigationView";
}
const XamlTypeDeclaration& NavigationView::declaration()
{
    return paneDeclaration("Velora.Fluent.NavigationView");
}
const PropertyDescriptor& NavigationView::property(std::string_view name) const
{
    return layoutProperty(className(), name);
}
std::string NavigationView::registerType(Framework& ui)
{
    return registerLayout(ui, declaration(), [] { return createElement<NavigationView>(); });
}
const PropertyDescriptor& NavigationView::paneProperty(usize index) const
{
    return index == 0 ? *elementProperties().props.Content : internalPane();
}
std::shared_ptr<UIElement> NavigationView::content() const
{
    return paneObject(0);
}
PropertySetResult NavigationView::setContent(std::shared_ptr<UIElement> value)
{
    return setPaneObject(0, std::move(value));
}
const PropertyDescriptor& NavigationView::selectedItemProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("NavigationView.SelectedItem", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
std::shared_ptr<NavigationViewItem> NavigationView::selectedItem() const
{
    const auto& value = getValue(selectedItemProperty());
    if (!value.isObject() || !value.asObject())
        return {};
    const auto& ref = value.asObject();
    return std::dynamic_pointer_cast<NavigationViewItem>(
        std::shared_ptr<UIElement>{ref.owner, static_cast<UIElement*>(ref.object)});
}
PropertySetResult NavigationView::setSelectedItem(std::shared_ptr<NavigationViewItem> value)
{
    return setValue(selectedItemProperty(), ObjectRef::share<UIElement>(std::move(value)));
}
const PropertyDescriptor& NavigationView::displayModeProperty()
{
    return modeKey().property();
}
std::string NavigationView::displayMode() const
{
    return getValue(displayModeProperty()).asString();
}
#define VELORA_NAV_ACCESSOR(Type, getter, Setter, name, accessor)                                            \
    Type NavigationView::getter() const                                                                      \
    {                                                                                                        \
        return getValue(property(name)).accessor();                                                          \
    }                                                                                                        \
    PropertySetResult NavigationView::set##Setter(Type value)                                                \
    {                                                                                                        \
        return setValue(property(name), std::move(value));                                                   \
    }
VELORA_NAV_ACCESSOR(bool, isPaneOpen, IsPaneOpen, "IsPaneOpen", asBool)
VELORA_NAV_ACCESSOR(std::string, paneDisplayMode, PaneDisplayMode, "PaneDisplayMode", asString)
VELORA_NAV_ACCESSOR(std::string, paneTitle, PaneTitle, "PaneTitle", asString)
VELORA_NAV_ACCESSOR(f64, compactModeThresholdWidth, CompactModeThresholdWidth, "CompactModeThresholdWidth",
                    asDouble)
VELORA_NAV_ACCESSOR(f64, expandedModeThresholdWidth, ExpandedModeThresholdWidth, "ExpandedModeThresholdWidth",
                    asDouble)
VELORA_NAV_ACCESSOR(f64, compactPaneLength, CompactPaneLength, "CompactPaneLength", asDouble)
VELORA_NAV_ACCESSOR(f64, openPaneLength, OpenPaneLength, "OpenPaneLength", asDouble)
VELORA_NAV_ACCESSOR(bool, isSettingsVisible, IsSettingsVisible, "IsSettingsVisible", asBool)
VELORA_NAV_ACCESSOR(bool, isBackEnabled, IsBackEnabled, "IsBackEnabled", asBool)
VELORA_NAV_ACCESSOR(std::string, isBackButtonVisible, IsBackButtonVisible, "IsBackButtonVisible", asString)
VELORA_NAV_ACCESSOR(bool, isPaneToggleButtonVisible, IsPaneToggleButtonVisible, "IsPaneToggleButtonVisible",
                    asBool)
#undef VELORA_NAV_ACCESSOR
const std::vector<std::shared_ptr<NavigationViewItem>>& NavigationView::menuItems() const noexcept
{
    return m_items;
}
std::shared_ptr<NavigationViewItem> NavigationView::settingsItem() const
{
    return m_settings;
}
std::vector<std::shared_ptr<UIElement>> NavigationView::ownedContentObjects() const
{
    auto result = PaneLayout::ownedContentObjects();
    result.insert(result.end(), m_items.begin(), m_items.end());
    result.push_back(m_settings);
    return result;
}
void NavigationView::setMenuItems(std::vector<std::shared_ptr<NavigationViewItem>> items)
{
    if (!isOwnerThread() || m_busy || m_updating)
        throw std::logic_error("Navigation menu is busy or belongs to another thread");
    std::unordered_set<const NavigationViewItem*> seen;
    auto* ui = attachedFramework();
    for (const auto& item : items)
        if (!item || item == m_settings || !item->isOwnerThread() || !seen.insert(item.get()).second ||
            (item->isInTree() && (!ui || std::ranges::find(m_items, item) == m_items.end() ||
                                  !ui->isWithin(ui->tree().element(*item), m_menuHost))))
            throw std::invalid_argument("Navigation requires distinct, detached menu items");
    const auto keepAlive = ui ? ui->tree().object(m_element) : nullptr;
    {
        Busy busy(m_busy);
        const auto previous = m_items;
        m_items = std::move(items);
        const auto selected = selectedItem();
        if (selected && selected != m_settings && std::ranges::find(m_items, selected) == m_items.end())
            setSelectedItem(nullptr);
        for (const auto& item : previous)
            if (std::ranges::find(m_items, item) == m_items.end())
                item->setPresentation(false, false);
    }
    rebuildMenu();
}
bool NavigationView::validateLayoutProperty(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == selectedItemProperty().id)
    {
        if (!value.isObject())
            return false;
        const auto& ref = value.asObject();
        if (!ref)
            return true;
        if (!ref.owner || ref.owner.get() != ref.object || !ref.type || *ref.type != typeid(UIElement))
            return false;
        const auto* item = dynamic_cast<NavigationViewItem*>(static_cast<UIElement*>(ref.object));
        return item && (item == m_settings.get() || std::ranges::any_of(m_items, [item](const auto& owned)
                                                                        { return owned.get() == item; }));
    }
    for (const auto& declared : declaration().properties)
    {
        if (declared.id() != p.id)
            continue;
        if (declared.kind == ValueKind::kBoolean)
            return value.isBool();
        if (declared.kind == ValueKind::kDouble)
            return value.isDouble() && std::isfinite(value.asDouble()) && value.asDouble() > 0 &&
                   value.asDouble() <= std::numeric_limits<float>::max();
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
        if (declared.name == "PaneDisplayMode")
            return value.asString() == "Auto" || value.asString() == "Left" ||
                   value.asString() == "LeftCompact" || value.asString() == "LeftMinimal" ||
                   value.asString() == "Top";
        if (declared.name == "IsBackButtonVisible")
            return value.asString() == "Auto" || value.asString() == "Visible" ||
                   value.asString() == "Collapsed";
    }
    return true;
}
void NavigationView::onAttached(Framework& ui, ElementId self)
{
    m_element = self;
    setPaneObject(1, createElement(ElementKind::kStackPanel));
    PaneLayout::onAttached(ui, self);
    m_menuHost = ui.tree().element(*paneObject(1));
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/NavigationViewChromeView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("Navigation chrome: " + loaded.error);
    m_chrome = ui.tree().element(*loaded.root);
    const auto templateRoot = ui.tree().parent(paneHost(0));
    if (!ui.tree().reparent(templateRoot, m_chrome, ui.tree().children(templateRoot).size()))
    {
        ui.tree().remove(m_chrome);
        throw std::runtime_error("Navigation chrome failed to attach");
    }
    m_toggle = ui.tree().element(*loaded.names.at("PaneToggleButton"));
    m_back = ui.tree().element(*loaded.names.at("BackButton"));
    m_title = ui.tree().element(*loaded.names.at("PaneTitleText"));
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_events.push_back(ui.subscribeEvent(m_toggle, ui.events().Click,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                             {
                                                 auto& nav = static_cast<NavigationView&>(*owner);
                                                 nav.setIsPaneOpen(!nav.isPaneOpen());
                                             }
                                         }));
    m_events.push_back(ui.subscribeEvent(m_back, ui.events().Click,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                             {
                                                 auto& nav = static_cast<NavigationView&>(*owner);
                                                 const auto handlers = nav.m_backHandlers;
                                                 for (const auto& [token, handler] : handlers)
                                                     if (nav.m_backHandlers.contains(token))
                                                         handler();
                                             }
                                         }));
    rebuildMenu();
}
void NavigationView::onDetached() noexcept
{
    m_events.clear();
    m_menuEvents.clear();
    m_element = m_menuHost = m_chrome = m_toggle = m_back = m_title = 0;
    m_firstLayout = true;
    PaneLayout::onDetached();
}
void NavigationView::rebuildMenu()
{
    auto* ui = attachedFramework();
    if (!ui || !m_menuHost || m_busy)
        return;
    const auto keepAlive = ui->tree().object(m_element);
    {
        Busy busy(m_busy);
        m_menuEvents.clear();
        const auto old = ui->tree().children(m_menuHost);
        for (const auto id : old)
            if (attachedFramework())
                ui->tree().remove(id);
        const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
        auto items = m_items;
        items.push_back(m_settings);
        for (const auto& item : items)
        {
            if (!attachedFramework())
                return;
            const auto id = ui->insert(item, m_menuHost);
            if (!id)
                throw std::runtime_error("Navigation item failed to attach");
            const std::weak_ptr<NavigationViewItem> weakItem = item;
            m_menuEvents.push_back(ui->subscribeEvent(id, ui->events().Click,
                                                      [weak, weakItem](ElementId, RoutedEventArgs&)
                                                      {
                                                          const auto owner = weak.lock();
                                                          const auto item = weakItem.lock();
                                                          if (owner && item)
                                                              static_cast<NavigationView&>(*owner).invokeItem(
                                                                  item);
                                                      }));
        }
    }
    updateParts();
}
bool NavigationView::paneOpenFor(std::string_view mode) const
{
    if (m_firstLayout || mode != displayMode())
        return mode == "Expanded" && !m_forceClosed;
    return isPaneOpen();
}
PaneLayoutPlan NavigationView::layoutPanes(Size available, const std::array<Size, 2>& desired)
{
    const auto setting = paneDisplayMode();
    const bool top = setting == "Top";
    const std::string_view mode = top || setting == "LeftMinimal"                   ? "Minimal"
                                  : setting == "Left"                               ? "Expanded"
                                  : setting == "LeftCompact"                        ? "Compact"
                                  : available.width >= expandedModeThresholdWidth() ? "Expanded"
                                  : available.width < compactModeThresholdWidth()   ? "Minimal"
                                                                                    : "Compact";
    const bool open = paneOpenFor(mode);
    PaneLayoutPlan plan;
    plan.mode = mode;
    plan.size = {std::isfinite(available.width) ? available.width
                                                : desired[0].width + static_cast<float>(openPaneLength()),
                 std::isfinite(available.height) ? available.height
                                                 : std::max(desired[0].height, desired[1].height) + 40};
    const auto w = plan.size.width, h = std::max(0.f, plan.size.height - 40);
    if (top)
    {
        plan.panes[1] = {0, 40, w, std::min(h, 44.f)};
        plan.panes[0] = {0, 40 + std::min(h, 44.f), w, std::max(0.f, h - 44)};
        return plan;
    }
    const float compact = mode == "Minimal" ? 0.f : static_cast<float>(compactPaneLength());
    const auto width = std::min(w, open ? static_cast<float>(openPaneLength()) : compact);
    const float reserved = mode == "Expanded" ? width : std::min(w, compact);
    plan.panes[0] = {reserved, 40, std::max(0.f, w - reserved), h};
    plan.panes[1] = {0, 40, width, h};
    plan.visible[1] = width > 0;
    plan.dismissOverlay = open && mode != "Expanded";
    return plan;
}
void NavigationView::onPlanArranged(const PaneLayoutPlan& plan)
{
    const bool open = paneOpenFor(plan.mode);
    m_firstLayout = false;
    {
        Busy adapting(m_adapting);
        setIsPaneOpen(open);
        setValue(modeKey(), std::string(plan.mode));
    }
    updateParts();
}
Size NavigationView::measureOverride(LayoutContext& context, ElementId self, const MeasureInput& input)
{
    if (m_chrome)
        context.measure(m_chrome, {input.content.width, 40});
    return PaneLayout::measureOverride(context, self, input);
}
void NavigationView::arrangeOverride(LayoutContext& context, ElementId self, Box content,
                                     const std::vector<ElementId>& children)
{
    PaneLayout::arrangeOverride(context, self, content, children);
    if (attachedFramework() && m_chrome)
        context.arrange(m_chrome, {0, 0, content.width, std::min(40.f, content.height)});
}
void NavigationView::updateParts()
{
    auto* ui = attachedFramework();
    if (!ui || !m_menuHost || m_updating)
        return;
    const auto keepAlive = ui->tree().object(m_element);
    Busy updating(m_updating);
    const bool top = paneDisplayMode() == "Top";
    const auto& props = elementProperties().props;
    const auto set = [&](ElementId id, const PropertyDescriptor* p, PropertyValue value)
    {
        if (attachedFramework() == ui)
            ui->set(id, p, std::move(value));
    };
    if (!isSettingsVisible() && selectedItem() == m_settings)
        setSelectedItem(nullptr);
    set(m_menuHost, props.Orientation, top ? "Horizontal" : "Vertical");
    set(m_toggle, props.Visibility, isPaneToggleButtonVisible() && !top);
    set(m_back, props.Visibility,
        isBackButtonVisible() == "Visible" ||
            (isBackButtonVisible() == "Auto" && displayMode() != "Expanded" && !top));
    set(m_back, props.IsEnabled, isBackEnabled());
    set(m_title, props.Text, paneTitle());
    auto items = m_items;
    items.push_back(m_settings);
    const auto selected = selectedItem();
    const bool compact = !top && !isPaneOpen();
    for (usize index = 0; attachedFramework() == ui && index < items.size(); ++index)
    {
        const auto& item = items[index];
        item->setPresentation(item == selected, compact);
        if (attachedFramework() != ui)
            break;
        const auto id = ui->tree().element(*item);
        set(id, props.Visibility, item != m_settings || isSettingsVisible());
        set(id, props.IsTabStop, item == selected || (!selected && index == 0));
    }
}
void NavigationView::invokeItem(const std::shared_ptr<NavigationViewItem>& item)
{
    auto* ui = attachedFramework();
    if (!ui)
        return;
    const auto keepAlive = ui->tree().object(m_element);
    if (item->selectsOnInvoked())
        setSelectedItem(item);
    if (!attachedFramework())
        return;
    const NavigationItemInvokedEventArgs args{item, item == m_settings};
    const auto handlers = m_invoked;
    for (const auto& [token, handler] : handlers)
        if (m_invoked.contains(token))
            handler(args);
    if (attachedFramework() && displayMode() != "Expanded" && paneDisplayMode() != "Top")
        setIsPaneOpen(false);
}
void NavigationView::onPointerPressed(InputContext&, PointerEventArgs& args)
{
    auto* ui = attachedFramework();
    if (ui && isPaneOpen() && displayMode() != "Expanded" && paneDisplayMode() != "Top" &&
        !ui->isWithin(args.originalSource, paneHost(1)) && !ui->isWithin(args.originalSource, m_chrome))
    {
        setIsPaneOpen(false);
        args.handled = true;
    }
}
void NavigationView::onKeyDown(InputContext&, KeyEventArgs& args)
{
    auto* ui = attachedFramework();
    if (!ui || args.ctrl || args.alt)
        return;
    if (args.key == Key::kEscape && isPaneOpen() && displayMode() != "Expanded" && paneDisplayMode() != "Top")
    {
        setIsPaneOpen(false);
        args.handled = true;
        return;
    }
    if (!ui->isWithin(ui->focused(), m_menuHost))
        return;
    std::vector<ElementId> stops;
    auto items = m_items;
    items.push_back(m_settings);
    for (const auto& item : items)
    {
        const auto id = ui->tree().element(*item);
        if (id && ui->tree().property(id, *ui->props().Visibility).asBool() &&
            ui->tree().property(id, *ui->props().IsEnabled).asBool())
            stops.push_back(id);
    }
    if (stops.empty())
        return;
    const bool top = paneDisplayMode() == "Top";
    const auto found =
        std::ranges::find_if(stops, [ui](ElementId id) { return ui->isWithin(ui->focused(), id); });
    usize index = found == stops.end() ? 0 : static_cast<usize>(found - stops.begin());
    if (args.key == Key::kHome)
        index = 0;
    else if (args.key == Key::kEnd)
        index = stops.size() - 1;
    else if (args.key == (top ? Key::kRight : Key::kDown))
        index = std::min(index + 1, stops.size() - 1);
    else if (args.key == (top ? Key::kLeft : Key::kUp))
        index = index ? index - 1 : 0;
    else
        return;
    ui->focus(stops[index]);
    args.handled = true;
}
u64 NavigationView::addSelectionChanged(std::function<void(std::shared_ptr<NavigationViewItem>)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue&)
        {
            if (p.id == selectedItemProperty().id)
                handler(selectedItem());
        });
}
u64 NavigationView::addDisplayModeChanged(std::function<void(std::string)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue&)
        {
            if (p.id == displayModeProperty().id)
                handler(displayMode());
        });
}
u64 NavigationView::nextToken()
{
    if (!isOwnerThread())
        throw std::logic_error("Navigation events belong to its UI thread");
    if (!m_nextToken)
        throw std::overflow_error("Navigation event tokens exhausted");
    return m_nextToken++;
}
u64 NavigationView::addItemInvoked(std::function<void(const NavigationItemInvokedEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_invoked.emplace(token, std::move(handler));
    return token;
}
u64 NavigationView::addBackRequested(std::function<void()> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_backHandlers.emplace(token, std::move(handler));
    return token;
}
bool NavigationView::removeHandler(u64 token)
{
    return isOwnerThread() && (m_invoked.erase(token) || m_backHandlers.erase(token));
}
} // namespace velora
