// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System AutoSuggestBox implemented by Velora; theme/helper reference: porting/AutoSuggestBox/.
#include "FluentTypes.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/controls/Automation.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/AutoSuggestBox.hpp>
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
} // namespace
const XamlTypeDeclaration& AutoSuggestBox::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.AutoSuggestBox");
}
const PropertyDescriptor& AutoSuggestBox::property(std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto& p : declaration().properties)
        {
            const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
            registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                            PropertyFlags::kPaint);
        }
        return true;
    }();
    (void)initialized;
    return *registry.find(declaration().property(name)->runtimeName);
}
std::string AutoSuggestBox::className() const
{
    return "Velora.Fluent.AutoSuggestBox";
}
std::string AutoSuggestBox::registerType(Framework& ui)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration().properties)
        properties.push_back(&property(p.name));
    return ui.registerSourceControl(
        declaration(), [] { return createElement<AutoSuggestBox>(); }, properties);
}
AutoSuggestBox::AutoSuggestBox() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
            if (p.id == textProperty().id)
            {
                const AutoSuggestTextChangedEventArgs args{m_reason, text()};
                m_reason = AutoSuggestTextChangeReason::kProgrammaticChange;
                updateParts(true);
                const auto handlers = m_textHandlers;
                for (const auto& [token, handler] : handlers)
                    if (m_textHandlers.contains(token))
                        handler(args);
            }
            else if (p.id == property("IsSuggestionListOpen").id)
                syncPopup();
            else
                updateParts(false);
        });
}
const PropertyDescriptor& AutoSuggestBox::textProperty()
{
    return property("Text");
}
std::string AutoSuggestBox::text() const
{
    return getValue(textProperty()).asString();
}
PropertySetResult AutoSuggestBox::changeText(std::string value, AutoSuggestTextChangeReason reason)
{
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    const auto before = m_reason;
    m_reason = reason;
    struct Reset
    {
        AutoSuggestTextChangeReason& value;
        AutoSuggestTextChangeReason before;
        ~Reset()
        {
            value = before;
        }
    } reset{m_reason, before};
    return setValue(textProperty(), std::move(value));
}
PropertySetResult AutoSuggestBox::setText(std::string value)
{
    return changeText(std::move(value), AutoSuggestTextChangeReason::kProgrammaticChange);
}
#define VELORA_SUGGEST_TEXT(getter, Setter, name)                                                            \
    std::string AutoSuggestBox::getter() const                                                               \
    {                                                                                                        \
        return getValue(property(name)).asString();                                                          \
    }                                                                                                        \
    PropertySetResult AutoSuggestBox::set##Setter(std::string value)                                         \
    {                                                                                                        \
        return setValue(property(name), std::move(value));                                                   \
    }
VELORA_SUGGEST_TEXT(header, Header, "Header")
VELORA_SUGGEST_TEXT(placeholderText, PlaceholderText, "PlaceholderText")
VELORA_SUGGEST_TEXT(queryIconGlyph, QueryIconGlyph, "QueryIconGlyph")
#undef VELORA_SUGGEST_TEXT
#define VELORA_SUGGEST_BOOL(getter, Setter, name)                                                            \
    bool AutoSuggestBox::getter() const                                                                      \
    {                                                                                                        \
        return getValue(property(name)).asBool();                                                            \
    }                                                                                                        \
    PropertySetResult AutoSuggestBox::set##Setter(bool value)                                                \
    {                                                                                                        \
        return setValue(property(name), value);                                                              \
    }
VELORA_SUGGEST_BOOL(isSuggestionListOpen, IsSuggestionListOpen, "IsSuggestionListOpen")
VELORA_SUGGEST_BOOL(updateTextOnSelect, UpdateTextOnSelect, "UpdateTextOnSelect")
#undef VELORA_SUGGEST_BOOL
bool AutoSuggestBox::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    for (const auto& d : declaration().properties)
    {
        if (d.id() != p.id)
            continue;
        if (d.kind == ValueKind::kBoolean)
            return value.isBool();
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
const std::vector<std::string>& AutoSuggestBox::suggestions() const noexcept
{
    return m_suggestions;
}
void AutoSuggestBox::setSuggestions(std::vector<std::string> values)
{
    if (!isOwnerThread() || m_refreshing || m_syncingPopup)
        throw std::logic_error("Suggestion collection is busy or belongs to another thread");
    if (values.size() > static_cast<usize>(std::numeric_limits<i32>::max()))
        throw std::length_error("Too many suggestions");
    for (const auto& value : values)
        utf8ToUtf16(value);
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    const bool requested = isSuggestionListOpen();
    {
        Busy refreshing(m_refreshing);
        const auto flyout = m_flyout;
        if (flyout)
            flyout->hide();
        m_suggestions = std::move(values);
        m_highlight = -1;
    }
    const bool open = !m_suggestions.empty() && (requested || (m_ui && m_ui->focused() == m_editor));
    setIsSuggestionListOpen(open);
    syncPopup(); // The requested value can remain true while the old presenter was replaced.
}
void AutoSuggestBox::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/AutoSuggestBoxView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("AutoSuggestBox template: " + loaded.error);
    m_editor = ui.tree().element(*loaded.names.at("TextBox"));
    m_header = ui.tree().element(*loaded.names.at("HeaderText"));
    m_query = ui.tree().element(*loaded.names.at("QueryButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root),
                     {{"TextBox", m_editor}, {"QueryButton", m_query}});
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_events.push_back(ui.subscribeEvent(
        m_editor, ui.events().TextChanged,
        [weak](ElementId, RoutedEventArgs&)
        {
            if (const auto owner = weak.lock())
            {
                auto& box = static_cast<AutoSuggestBox&>(*owner);
                if (box.m_ui && !box.m_syncingEditor)
                    box.changeText(
                        box.m_ui->tree().property(box.m_editor, *box.m_ui->props().Text).asString(),
                        AutoSuggestTextChangeReason::kUserInput);
            }
        }));
    m_events.push_back(ui.subscribeEvent(m_query, ui.events().Click,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                                 static_cast<AutoSuggestBox&>(*owner).submitQuery();
                                         }));
    updateParts(true);
    syncPopup();
}
void AutoSuggestBox::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_editor = m_header = m_query = 0;
    m_events.clear();
    m_popupEvents.clear();
    m_buttons.clear();
    m_highlight = -1;
    const auto flyout = std::exchange(m_flyout, {});
    if (flyout)
        flyout->hide();
    try
    {
        writeListOpen(false);
    }
    catch (...)
    {
    }
}
void AutoSuggestBox::updateParts(bool changed)
{
    if (!m_ui || m_syncingEditor)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    Busy syncing(m_syncingEditor);
    const auto& props = elementProperties().props;
    const auto set = [this](ElementId id, const PropertyDescriptor* p, PropertyValue value)
    {
        if (m_ui)
            m_ui->set(id, p, std::move(value));
    };
    if (changed)
    {
        set(m_editor, props.Text, text());
        // Preserve editor positions, clamping them if external text becomes shorter.
        for (const auto* position : {props.CaretPosition, props.SelectionAnchor})
            if (m_ui && m_ui->tree().property(m_editor, *position).asInt() > static_cast<i64>(text().size()))
                set(m_editor, position, static_cast<i64>(text().size()));
    }
    set(m_header, props.Text, header());
    set(m_header, props.Visibility, !header().empty());
    set(m_editor, props.PlaceholderText, placeholderText());
    set(m_query, props.Content, queryIconGlyph());
    set(m_query, props.Visibility, !queryIconGlyph().empty());
}
void AutoSuggestBox::writeListOpen(bool open)
{
    const bool before = m_syncingPopup;
    m_syncingPopup = true;
    try
    {
        setIsSuggestionListOpen(open);
    }
    catch (...)
    {
        m_syncingPopup = before;
        throw;
    }
    m_syncingPopup = before;
}
void AutoSuggestBox::popupClosed()
{
    m_popupEvents.clear();
    m_buttons.clear();
    m_highlight = -1;
    if (!m_refreshing)
        writeListOpen(false);
}
void AutoSuggestBox::syncPopup()
{
    if (!m_ui || m_syncingPopup || m_refreshing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    Busy syncing(m_syncingPopup);
    if (!isSuggestionListOpen())
    {
        const auto flyout = m_flyout;
        if (flyout)
            flyout->hide();
        return;
    }
    if (m_flyout && m_flyout->isOpen())
        return;
    if (m_suggestions.empty())
    {
        writeListOpen(false);
        return;
    }
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    m_flyout = std::make_shared<Flyout>(
        [weak](Framework& ui)
        {
            const auto owner = weak.lock();
            if (!owner)
                throw std::runtime_error("AutoSuggestBox owner expired");
            return static_cast<AutoSuggestBox&>(*owner).buildSuggestions(ui);
        });
    PopupOptions options;
    options.nonModal = true;
    options.takeFocus = false;
    const auto flyout = m_flyout;
    if (!flyout->showAt(
            *m_ui, m_element,
            [weak](bool open)
            {
                if (!open)
                    if (const auto owner = weak.lock())
                        static_cast<AutoSuggestBox&>(*owner).popupClosed();
            },
            m_element, false, std::move(options)))
        writeListOpen(false);
}
ElementId AutoSuggestBox::buildSuggestions(Framework& ui)
{
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/AutoSuggestBoxSuggestionsView.vxbf"));
    if (!loaded.ok)
        throw std::runtime_error("AutoSuggestBox suggestions: " + loaded.error);
    const auto root = ui.tree().element(*loaded.root);
    const auto host = ui.tree().element(*loaded.names.at("SuggestionItems"));
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(m_element));
    try
    {
        ui.set(root, ui.props().Height, std::min(200.0, static_cast<double>(m_suggestions.size()) * 36));
        for (usize index = 0; index < m_suggestions.size(); ++index)
        {
            const auto button = ui.create(ElementKind::kButton, host);
            m_buttons.push_back(button);
            ui.set(button, ui.props().Content, m_suggestions[index]);
            ui.set(button, ui.props().MinHeight, 36.0);
            ui.set(button, ui.props().IsTabStop, false);
            m_popupEvents.push_back(ui.subscribeEvent(
                button, ui.events().Click,
                [weak, index](ElementId, RoutedEventArgs&)
                {
                    if (const auto owner = weak.lock())
                        static_cast<AutoSuggestBox&>(*owner).chooseSuggestion(static_cast<i32>(index));
                }));
        }
        return root;
    }
    catch (...)
    {
        ui.tree().remove(root);
        m_popupEvents.clear();
        m_buttons.clear();
        throw;
    }
}
void AutoSuggestBox::updateHighlight()
{
    const auto ids = m_buttons;
    for (usize index = 0; m_ui && index < ids.size(); ++index)
        m_ui->set(ids[index], m_ui->props().FontSize, static_cast<i32>(index) == m_highlight ? 18.0 : 14.0);
}
bool AutoSuggestBox::chooseSuggestion(i32 index)
{
    if (!isOwnerThread())
        throw std::logic_error("AutoSuggestBox belongs to its UI thread");
    if (!m_ui || index < 0 || static_cast<usize>(index) >= m_suggestions.size())
        return false;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto chosen = m_suggestions[index];
    const auto handlers = m_chosenHandlers;
    for (const auto& [token, handler] : handlers)
        if (m_chosenHandlers.contains(token))
            handler(chosen);
    if (!m_ui)
        return false;
    if (updateTextOnSelect())
        changeText(chosen, AutoSuggestTextChangeReason::kSuggestionChosen);
    if (!m_ui)
        return false;
    setIsSuggestionListOpen(false);
    if (m_ui)
        m_ui->focus(m_editor);
    if (m_ui)
        publishQuery(chosen);
    return true;
}
void AutoSuggestBox::publishQuery(std::optional<std::string> chosen)
{
    const AutoSuggestQuerySubmittedEventArgs args{text(), std::move(chosen)};
    const auto handlers = m_queryHandlers;
    for (const auto& [token, handler] : handlers)
        if (m_queryHandlers.contains(token))
            handler(args);
}
void AutoSuggestBox::submitQuery()
{
    if (!isOwnerThread())
        throw std::logic_error("AutoSuggestBox belongs to its UI thread");
    if (!m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    setIsSuggestionListOpen(false);
    if (m_ui)
        m_ui->focus(m_editor);
    if (m_ui)
        publishQuery(std::nullopt);
}
void AutoSuggestBox::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui || args.ctrl || args.alt)
        return;
    if (!m_ui->tree().property(m_editor, *m_ui->props().PreeditText).asString().empty())
        return;
    if (args.key == Key::kTab)
    {
        setIsSuggestionListOpen(false);
        return;
    }
    if (args.key == Key::kEnter)
    {
        if (isSuggestionListOpen() && m_highlight >= 0)
            chooseSuggestion(m_highlight);
        else
            submitQuery();
    }
    else if ((args.key == Key::kDown || args.key == Key::kUp) && !m_suggestions.empty())
    {
        setIsSuggestionListOpen(true);
        if (!m_ui)
            return;
        const auto count = static_cast<i32>(m_suggestions.size());
        m_highlight =
            m_highlight < 0
                ? (args.key == Key::kDown ? 0 : count - 1)
                : static_cast<i32>(
                      (static_cast<i64>(m_highlight) + (args.key == Key::kDown ? 1 : count - 1)) % count);
        updateHighlight();
    }
    else
        return;
    args.handled = true;
}
std::string AutoSuggestBox::automationName() const
{
    return header();
}
std::optional<AutomationControlType> AutoSuggestBox::automationControlType() const noexcept
{
    return AutomationControlType::kEdit;
}
u64 AutoSuggestBox::nextToken()
{
    if (!isOwnerThread())
        throw std::logic_error("AutoSuggestBox events belong to its UI thread");
    if (!m_nextToken)
        throw std::overflow_error("AutoSuggestBox event tokens exhausted");
    return m_nextToken++;
}
u64 AutoSuggestBox::addTextChanged(std::function<void(const AutoSuggestTextChangedEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_textHandlers.emplace(token, std::move(handler));
    return token;
}
u64 AutoSuggestBox::addSuggestionChosen(std::function<void(const std::string&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_chosenHandlers.emplace(token, std::move(handler));
    return token;
}
u64 AutoSuggestBox::addQuerySubmitted(std::function<void(const AutoSuggestQuerySubmittedEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_queryHandlers.emplace(token, std::move(handler));
    return token;
}
bool AutoSuggestBox::removeHandler(u64 token)
{
    return isOwnerThread() &&
           (m_textHandlers.erase(token) || m_chosenHandlers.erase(token) || m_queryHandlers.erase(token));
}
} // namespace velora
