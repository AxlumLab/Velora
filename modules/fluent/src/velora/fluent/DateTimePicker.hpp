// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/fluent/Flyout.hpp>
namespace velora
{
// Shared implementation of the system DatePicker/TimePicker contracts whose WinUI 2.3
// source distribution contains themes only. Draft fields exist only for an open transaction.
class DateTimePicker : public UIElement
{
  public:
    ~DateTimePicker() override;
    std::string className() const override;
    std::string header() const;
    PropertySetResult setHeader(std::string);
    bool openPicker();
    void acceptPicker();
    void cancelPicker();
    bool isPickerOpen() const noexcept;
    const std::string& lastError() const noexcept;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;
    std::string automationName() const override;

  protected:
    explicit DateTimePicker(bool time);
    static const XamlTypeDeclaration& pickerDeclaration(bool time);
    static std::string registerPicker(Framework&, bool time, SourceControlRegistry::Factory);
    static const PropertyDescriptor& pickerProperty(bool time, std::string_view);
    const PropertyDescriptor& property(std::string_view) const;
    static std::optional<std::chrono::year_month_day> parseDate(std::string_view);
    static std::string formatDate(std::chrono::year_month_day);
    static std::optional<std::chrono::minutes> parseTime(std::string_view);
    static std::string formatTime(std::chrono::minutes);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    bool canOpen() const;
    void updateParts();
    void initializeDraft();
    void stepField(usize, i32);
    void updateDraft();
    ElementId buildPresenter(Framework&);
    void presenterClosed();
    const bool m_time;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_header = 0, m_button = 0, m_presenter = 0;
    std::array<ElementId, 3> m_fields{}, m_labels{}, m_values{};
    std::array<i32, 3> m_draft{};
    std::vector<EventSubscription> m_events, m_presenterEvents;
    std::shared_ptr<Flyout> m_flyout;
    bool m_opening = false;
};
} // namespace velora
