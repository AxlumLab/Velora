// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// WinUI 2.3 PersonPicture behavior incorporated into Velora; see WinUI23.provenance.json.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{
// Internal Fluent control. Contact and PreferSmallImage await a contact/image service; the
// supported image properties use resource/file URI strings instead of WinRT ImageSource.
class PersonPicture final : public UIElement
{
  public:
    PersonPicture();
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);

#define VELORA_PERSON_STRING(method, setter, name)                                                           \
    [[nodiscard]] std::string method() const;                                                                \
    PropertySetResult setter(std::string value);                                                             \
    [[nodiscard]] static const PropertyDescriptor& method##Property();
    VELORA_PERSON_STRING(displayName, setDisplayName, DisplayName)
    VELORA_PERSON_STRING(initials, setInitials, Initials)
    VELORA_PERSON_STRING(profilePicture, setProfilePicture, ProfilePicture)
    VELORA_PERSON_STRING(badgeGlyph, setBadgeGlyph, BadgeGlyph)
    VELORA_PERSON_STRING(badgeImageSource, setBadgeImageSource, BadgeImageSource)
    VELORA_PERSON_STRING(badgeText, setBadgeText, BadgeText)
#undef VELORA_PERSON_STRING
    [[nodiscard]] bool isGroup() const;
    PropertySetResult setIsGroup(bool value);
    [[nodiscard]] static const PropertyDescriptor& isGroupProperty();
    [[nodiscard]] i32 badgeNumber() const;
    PropertySetResult setBadgeNumber(i32 value);
    [[nodiscard]] static const PropertyDescriptor& badgeNumberProperty();

    [[nodiscard]] std::string actualInitials() const;
    [[nodiscard]] std::string visualState() const;
    [[nodiscard]] std::string badgeState() const;
    [[nodiscard]] std::string badgeLabel() const;
    [[nodiscard]] std::string automationName() const override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

    void onAttached(Framework& ui, ElementId self) override;
    void onDetached() noexcept override;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    static const PropertyDescriptor& property(std::string_view name);
    void updateParts();
    void resizeParts(f32 diameter);
    void setPart(std::string_view name, const PropertyDescriptor*, PropertyValue value);
    Framework* m_ui = nullptr; // borrowed only while attached; cleared on removal
    ElementId m_element = 0, m_root = 0;
    std::unordered_map<std::string, ElementId> m_parts;
};
} // namespace velora
