// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// TeachingTip action/close/closing flow adapted from WinUI 2.3 TeachingTip.cpp.
// ContentDialog system behavior is implemented by Velora; template reference: porting/ContentDialog/.
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/ContentDialog.hpp>
#include <velora/fluent/TeachingTip.hpp>
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
} // namespace
const PropertyDescriptor& PopupPanel::panelProperty(bool teaching, std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto* type : {"ContentDialog", "TeachingTip"})
        {
            for (const auto* name : {"Title", "Content", "Subtitle", "PrimaryButtonText",
                                     "SecondaryButtonText", "CloseButtonText"})
            {
                const std::string_view resolved =
                    std::string_view(type) == "TeachingTip" && std::string_view(name) == "PrimaryButtonText"
                        ? "ActionButtonContent"
                    : std::string_view(type) == "TeachingTip" && std::string_view(name) == "CloseButtonText"
                        ? "CloseButtonContent"
                        : name;
                registry.define(std::string(type) + "." + std::string(resolved), "", PropertyFlags::kPaint);
            }
            registry.define(std::string(type) + ".DefaultButton", "None", PropertyFlags::kPaint);
            for (const auto* name : {"IsPrimaryButtonEnabled", "IsSecondaryButtonEnabled"})
                registry.define(std::string(type) + "." + name, true, PropertyFlags::kPaint);
            registry.define(std::string(type) + ".IsOpen", false, PropertyFlags::kPaint);
            registry.define(std::string(type) + ".IsLightDismissEnabled", false, PropertyFlags::kPaint);
        }
        return true;
    }();
    (void)initialized;
    if (teaching && name == "PrimaryButtonText")
        name = "ActionButtonContent";
    if (teaching && name == "CloseButtonText")
        name = "CloseButtonContent";
    return *registry.find(std::string(teaching ? "TeachingTip." : "ContentDialog.") + std::string(name));
}
const PropertyDescriptor& PopupPanel::property(std::string_view name) const
{
    return panelProperty(m_teaching, name);
}
PopupPanel::PopupPanel(bool teaching) : m_teaching(teaching)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (m_syncing)
                return;
            if (p.id == property("IsOpen").id)
                syncOpen();
            else
                updateParts();
        });
}
PopupPanel::~PopupPanel()
{
    if (!isOwnerThread())
        std::terminate();
    if (m_flyout)
        m_flyout->hide();
}
std::string PopupPanel::title() const
{
    return getValue(property("Title")).asString();
}
PropertySetResult PopupPanel::setTitle(std::string value)
{
    return setValue(property("Title"), std::move(value));
}
std::string PopupPanel::content() const
{
    return getValue(property("Content")).asString();
}
PropertySetResult PopupPanel::setContent(std::string value)
{
    return setValue(property("Content"), std::move(value));
}
bool PopupPanel::isOpen() const noexcept
{
    return m_flyout && m_flyout->isOpen();
}
PanelResult PopupPanel::result() const noexcept
{
    return m_result;
}
const std::string& PopupPanel::lastError() const noexcept
{
    static const std::string empty;
    return m_flyout ? m_flyout->lastError() : empty;
}
bool PopupPanel::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == property("IsOpen").id || p.id == property("IsLightDismissEnabled").id ||
        p.id == property("IsPrimaryButtonEnabled").id || p.id == property("IsSecondaryButtonEnabled").id)
        return value.isBool();
    if (!p.name.starts_with(m_teaching ? "TeachingTip." : "ContentDialog."))
        return true;
    if (!value.isString())
        return false;
    if (p.id == property("DefaultButton").id)
        return value.asString() == "None" || value.asString() == "Primary" ||
               value.asString() == "Secondary" || value.asString() == "Close";
    try
    {
        utf8ToUtf16(value.asString());
    }
    catch (const std::exception&)
    {
        return false;
    }
    return true;
}
void PopupPanel::attachTarget(Framework& ui, ElementId anchor)
{
    if (!isOwnerThread() || !ui.isOwnerThread() || !ui.tree().contains(anchor) ||
        weak_from_this().expired() || isOpen() || m_notifying || m_syncing)
        throw std::invalid_argument(
            "Popup panel requires a shared owner and a live UI-thread target; close before retargeting");
    m_ui = &ui;
    m_lifetime = ui.lifetime();
    m_anchor = anchor;
    const auto weak = weak_from_this();
    ui.tree().onRemoved(anchor,
                        [weak, anchor, host = &ui]
                        {
                            if (const auto owner = weak.lock();
                                owner && owner->m_ui == host && owner->m_anchor == anchor)
                                owner->detachTarget();
                        });
    if (m_teaching && getValue(property("IsOpen")).asBool())
        syncOpen();
}
void PopupPanel::detachTarget()
{
    if (!isOwnerThread())
        throw std::logic_error("Popup panel belongs to its UI thread");
    const auto flyout = m_flyout;
    if (flyout)
        flyout->hide();
    m_ui = nullptr;
    m_lifetime.reset();
    m_anchor = 0;
    writeOpen(false);
}
bool PopupPanel::showPanel(Framework& ui, ElementId anchor)
{
    if (isOpen())
        return false;
    attachTarget(ui, anchor);
    setValue(property("IsOpen"), true);
    // A previously requested TeachingTip can already have IsOpen=true before attachment.
    if (!isOpen())
        syncOpen();
    return isOpen();
}
void PopupPanel::writeOpen(bool open)
{
    const bool previous = m_syncing;
    m_syncing = true;
    try
    {
        setValue(property("IsOpen"), open);
    }
    catch (...)
    {
        m_syncing = previous;
        throw;
    }
    m_syncing = previous;
}
void PopupPanel::syncOpen()
{
    if (m_syncing)
        return;
    const auto keepAlive = weak_from_this().lock();
    if (!getValue(property("IsOpen")).asBool())
    {
        if (isOpen() && !requestClose(PanelCloseReason::kProgrammatic, PanelResult::kNone))
            writeOpen(true);
        return;
    }
    if (isOpen() || !m_ui || m_lifetime.expired())
        return;
    Busy syncing(m_syncing);
    const auto weak = weak_from_this();
    m_result = PanelResult::kNone;
    m_reason = PanelCloseReason::kProgrammatic;
    m_flyout = std::make_shared<Flyout>(
        [weak](Framework& ui)
        {
            const auto owner = weak.lock();
            if (!owner)
                throw std::runtime_error("Popup panel owner expired");
            return owner->buildPresenter(ui);
        });
    PopupOptions options;
    options.centered = !m_teaching;
    options.nonModal = m_teaching;
    options.lightDismiss = m_teaching && getValue(property("IsLightDismissEnabled")).asBool();
    options.dismissRequested = [weak]
    {
        const auto owner = weak.lock();
        return owner && owner->canClose(PanelCloseReason::kLightDismiss, PanelResult::kNone);
    };
    const auto flyout = m_flyout;
    if (!flyout->showAt(
            *m_ui, m_anchor,
            [weak](bool open)
            {
                if (!open)
                    if (const auto owner = weak.lock())
                        owner->closed();
            },
            0, false, std::move(options)))
    {
        m_events.clear();
        m_parts.clear();
        m_root = 0;
        writeOpen(false);
        return;
    }
    writeOpen(true);
    if (!m_teaching && m_ui && !m_lifetime.expired())
    {
        const auto preferred = getValue(property("DefaultButton")).asString();
        if (preferred != "None")
        {
            const auto id = m_parts.at(preferred + "Button");
            if (m_ui->tree().property(id, *m_ui->props().Visibility).asBool() &&
                m_ui->tree().property(id, *m_ui->props().IsEnabled).asBool())
                m_ui->focus(id);
        }
    }
}
ElementId PopupPanel::buildPresenter(Framework& ui)
{
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/PopupPanelView.vxbf"));
    if (!loaded.ok)
        throw std::runtime_error("Popup panel template: " + loaded.error);
    const auto root = ui.tree().element(*loaded.root);
    m_root = root;
    for (const auto* name :
         {"TitleText", "SubtitleText", "BodyText", "PrimaryButton", "SecondaryButton", "CloseButton"})
        m_parts[name] = ui.tree().element(*loaded.names.at(name));
    const auto weak = weak_from_this();
    try
    {
        updateParts();
        for (const auto button : {PanelButton::kPrimary, PanelButton::kSecondary, PanelButton::kClose})
        {
            const char* name = button == PanelButton::kPrimary     ? "PrimaryButton"
                               : button == PanelButton::kSecondary ? "SecondaryButton"
                                                                   : "CloseButton";
            m_events.push_back(ui.subscribeEvent(m_parts.at(name), ui.events().Click,
                                                 [weak, button](ElementId, RoutedEventArgs&)
                                                 {
                                                     if (const auto owner = weak.lock())
                                                         owner->invoke(button);
                                                 }));
        }
        return root;
    }
    catch (...)
    {
        ui.tree().remove(root);
        m_parts.clear();
        m_events.clear();
        m_root = 0;
        throw;
    }
}
void PopupPanel::updateParts()
{
    if (!m_ui || m_lifetime.expired() || !m_root)
        return;
    const auto keepAlive = shared_from_this();
    auto* ui = m_ui;
    const auto lifetime = m_lifetime;
    const auto root = m_root;
    const auto& props = elementProperties().props;
    const auto set = [&](std::string_view name, const PropertyDescriptor* p, PropertyValue value)
    {
        if (!lifetime.expired() && m_ui == ui && m_root == root && m_parts.contains(name))
            ui->set(m_parts.find(name)->second, p, std::move(value));
    };
    for (const auto& [part, value] : {std::pair{"TitleText", title()},
                                      {"SubtitleText", getValue(property("Subtitle")).asString()},
                                      {"BodyText", content()}})
    {
        set(part, props.Text, value);
        set(part, props.Visibility, !value.empty());
    }
    for (const auto* button : {"Primary", "Secondary", "Close"})
    {
        const auto text = getValue(property(std::string(button) + "ButtonText")).asString();
        const auto name = std::string(button) + "Button";
        set(name, props.Content, text.empty() && m_teaching && name == "CloseButton" ? "Close" : text);
        set(name, props.Visibility, !text.empty() || (m_teaching && name == "CloseButton"));
        if (name != "CloseButton")
            set(name, props.IsEnabled, getValue(property("Is" + name + "Enabled")));
    }
    if (!lifetime.expired() && m_ui == ui && m_root == root && isOpen())
        ui->setPopupLightDismiss(ui->popupRoot(),
                                 m_teaching && getValue(property("IsLightDismissEnabled")).asBool());
}
bool PopupPanel::canClose(PanelCloseReason reason, PanelResult result)
{
    if (m_notifying)
        return false;
    const auto keepAlive = shared_from_this();
    Busy notifying(m_notifying);
    const auto flyout = m_flyout;
    PanelClosingEventArgs args{reason, result};
    const auto handlers = m_closing;
    for (const auto& [token, handler] : handlers)
        if (m_closing.contains(token))
            handler(args);
    if (args.cancel || m_flyout != flyout || !isOpen())
        return false;
    m_reason = reason;
    m_result = result;
    return true;
}
bool PopupPanel::requestClose(PanelCloseReason reason, PanelResult result)
{
    const auto keepAlive = shared_from_this();
    if (!isOpen())
    {
        writeOpen(false);
        return true;
    }
    const auto flyout = m_flyout;
    if (!canClose(reason, result))
        return false;
    if (m_flyout == flyout)
        flyout->hide();
    return !isOpen();
}
bool PopupPanel::hide()
{
    if (!isOwnerThread())
        throw std::logic_error("Popup panel belongs to its UI thread");
    return requestClose(PanelCloseReason::kProgrammatic, PanelResult::kNone);
}
void PopupPanel::invoke(PanelButton button)
{
    const auto keepAlive = shared_from_this();
    const auto flyout = m_flyout;
    PanelButtonEventArgs args{button};
    const auto handlers = m_buttons;
    for (const auto& [token, handler] : handlers)
        if (m_buttons.contains(token))
            handler(args);
    if (args.cancel || m_flyout != flyout || !isOpen() || (m_teaching && button == PanelButton::kPrimary))
        return;
    requestClose(PanelCloseReason::kCloseButton, m_teaching || button == PanelButton::kClose
                                                     ? PanelResult::kNone
                                                 : button == PanelButton::kPrimary ? PanelResult::kPrimary
                                                                                   : PanelResult::kSecondary);
}
void PopupPanel::closed()
{
    m_events.clear();
    m_parts.clear();
    m_root = 0;
    writeOpen(false);
    const auto handlers = m_closed;
    const auto reason = m_reason;
    const auto result = m_result;
    for (const auto& [token, handler] : handlers)
        if (m_closed.contains(token))
            handler(reason, result);
}
u64 PopupPanel::nextToken()
{
    if (!isOwnerThread())
        throw std::logic_error("Popup panel events belong to its UI thread");
    if (!m_nextToken)
        throw std::overflow_error("Popup panel event tokens exhausted");
    return m_nextToken++;
}
u64 PopupPanel::addButtonClick(std::function<void(PanelButtonEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_buttons.emplace(token, std::move(handler));
    return token;
}
u64 PopupPanel::addClosing(std::function<void(PanelClosingEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_closing.emplace(token, std::move(handler));
    return token;
}
u64 PopupPanel::addClosed(std::function<void(PanelCloseReason, PanelResult)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_closed.emplace(token, std::move(handler));
    return token;
}
bool PopupPanel::removeHandler(u64 token)
{
    if (!isOwnerThread())
        return false;
    return m_buttons.erase(token) || m_closing.erase(token) || m_closed.erase(token);
}
ContentDialog::ContentDialog() : PopupPanel(false)
{
}
bool ContentDialog::showAt(Framework& ui, ElementId anchor)
{
    return showPanel(ui, anchor);
}
TeachingTip::TeachingTip() : PopupPanel(true)
{
}
void TeachingTip::attach(Framework& ui, ElementId target)
{
    attachTarget(ui, target);
}
void TeachingTip::detach()
{
    detachTarget();
}
const PropertyDescriptor& TeachingTip::isOpenProperty()
{
    return panelProperty(true, "IsOpen");
}
PropertySetResult TeachingTip::setIsOpen(bool value)
{
    return setValue(isOpenProperty(), value);
}
#define VELORA_PANEL_TEXT(Type, getter, Setter, name)                                                        \
    std::string Type::getter() const                                                                         \
    {                                                                                                        \
        return getValue(property(name)).asString();                                                          \
    }                                                                                                        \
    PropertySetResult Type::set##Setter(std::string value)                                                   \
    {                                                                                                        \
        return setValue(property(name), std::move(value));                                                   \
    }
VELORA_PANEL_TEXT(ContentDialog, primaryButtonText, PrimaryButtonText, "PrimaryButtonText")
VELORA_PANEL_TEXT(ContentDialog, secondaryButtonText, SecondaryButtonText, "SecondaryButtonText")
VELORA_PANEL_TEXT(ContentDialog, closeButtonText, CloseButtonText, "CloseButtonText")
VELORA_PANEL_TEXT(ContentDialog, defaultButton, DefaultButton, "DefaultButton")
VELORA_PANEL_TEXT(TeachingTip, subtitle, Subtitle, "Subtitle")
VELORA_PANEL_TEXT(TeachingTip, actionButtonContent, ActionButtonContent, "PrimaryButtonText")
VELORA_PANEL_TEXT(TeachingTip, closeButtonContent, CloseButtonContent, "CloseButtonText")
#undef VELORA_PANEL_TEXT
#define VELORA_PANEL_BOOL(Type, getter, Setter, name)                                                        \
    bool Type::getter() const                                                                                \
    {                                                                                                        \
        return getValue(property(name)).asBool();                                                            \
    }                                                                                                        \
    PropertySetResult Type::set##Setter(bool value)                                                          \
    {                                                                                                        \
        return setValue(property(name), value);                                                              \
    }
VELORA_PANEL_BOOL(ContentDialog, isPrimaryButtonEnabled, IsPrimaryButtonEnabled, "IsPrimaryButtonEnabled")
VELORA_PANEL_BOOL(ContentDialog, isSecondaryButtonEnabled, IsSecondaryButtonEnabled,
                  "IsSecondaryButtonEnabled")
VELORA_PANEL_BOOL(TeachingTip, isLightDismissEnabled, IsLightDismissEnabled, "IsLightDismissEnabled")
#undef VELORA_PANEL_BOOL
} // namespace velora
