// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/fluent/Flyout.hpp>
namespace velora
{
enum class AutoSuggestTextChangeReason : u8
{
    kUserInput,
    kProgrammaticChange,
    kSuggestionChosen
};
struct AutoSuggestTextChangedEventArgs
{
    AutoSuggestTextChangeReason reason;
    std::string text;
};
struct AutoSuggestQuerySubmittedEventArgs
{
    std::string queryText;
    std::optional<std::string> chosenSuggestion;
};

class AutoSuggestBox final : public UIElement
{
  public:
    AutoSuggestBox();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& textProperty();
    std::string text() const;
    PropertySetResult setText(std::string);
    std::string header() const;
    PropertySetResult setHeader(std::string);
    std::string placeholderText() const;
    PropertySetResult setPlaceholderText(std::string);
    std::string queryIconGlyph() const;
    PropertySetResult setQueryIconGlyph(std::string);
    bool isSuggestionListOpen() const;
    PropertySetResult setIsSuggestionListOpen(bool);
    bool updateTextOnSelect() const;
    PropertySetResult setUpdateTextOnSelect(bool);
    void setSuggestions(std::vector<std::string>);
    const std::vector<std::string>& suggestions() const noexcept;
    bool chooseSuggestion(i32 index);
    void submitQuery();
    u64 addTextChanged(std::function<void(const AutoSuggestTextChangedEventArgs&)>);
    u64 addSuggestionChosen(std::function<void(const std::string&)>);
    u64 addQuerySubmitted(std::function<void(const AutoSuggestQuerySubmittedEventArgs&)>);
    bool removeHandler(u64);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    std::string automationName() const override;
    std::optional<AutomationControlType> automationControlType() const noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    static const PropertyDescriptor& property(std::string_view);
    PropertySetResult changeText(std::string, AutoSuggestTextChangeReason);
    void updateParts(bool textChanged);
    void syncPopup();
    void writeListOpen(bool);
    void popupClosed();
    ElementId buildSuggestions(Framework&);
    void updateHighlight();
    void publishQuery(std::optional<std::string> chosen);
    u64 nextToken();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_editor = 0, m_header = 0, m_query = 0;
    bool m_syncingEditor = false, m_syncingPopup = false, m_refreshing = false;
    AutoSuggestTextChangeReason m_reason = AutoSuggestTextChangeReason::kProgrammaticChange;
    i32 m_highlight = -1;
    std::vector<std::string> m_suggestions;
    std::vector<ElementId> m_buttons;
    std::vector<EventSubscription> m_events, m_popupEvents;
    std::shared_ptr<Flyout> m_flyout;
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(const AutoSuggestTextChangedEventArgs&)>> m_textHandlers;
    std::map<u64, std::function<void(const std::string&)>> m_chosenHandlers;
    std::map<u64, std::function<void(const AutoSuggestQuerySubmittedEventArgs&)>> m_queryHandlers;
};
} // namespace velora
