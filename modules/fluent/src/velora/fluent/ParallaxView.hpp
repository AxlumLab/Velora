// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <velora/controls/ContentElement.hpp>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
namespace velora
{
class ParallaxView final : public ContentElement
{
  public:
    ParallaxView();
    ~ParallaxView() override;
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& childProperty();
    std::shared_ptr<UIElement> child() const;
    PropertySetResult setChild(std::shared_ptr<UIElement>);
    std::shared_ptr<UIElement> source() const;
    void setSource(std::shared_ptr<UIElement>);
#define VELORA_PARALLAX_PROPERTY(Type, getter, Setter)                                                       \
    Type getter() const;                                                                                     \
    PropertySetResult set##Setter(Type);
    VELORA_PARALLAX_PROPERTY(f64, horizontalShift, HorizontalShift)
    VELORA_PARALLAX_PROPERTY(f64, verticalShift, VerticalShift)
    VELORA_PARALLAX_PROPERTY(f64, horizontalSourceStartOffset, HorizontalSourceStartOffset)
    VELORA_PARALLAX_PROPERTY(f64, horizontalSourceEndOffset, HorizontalSourceEndOffset)
    VELORA_PARALLAX_PROPERTY(f64, verticalSourceStartOffset, VerticalSourceStartOffset)
    VELORA_PARALLAX_PROPERTY(f64, verticalSourceEndOffset, VerticalSourceEndOffset)
    VELORA_PARALLAX_PROPERTY(f64, maxHorizontalShiftRatio, MaxHorizontalShiftRatio)
    VELORA_PARALLAX_PROPERTY(f64, maxVerticalShiftRatio, MaxVerticalShiftRatio)
    VELORA_PARALLAX_PROPERTY(std::string, horizontalSourceOffsetKind, HorizontalSourceOffsetKind)
    VELORA_PARALLAX_PROPERTY(std::string, verticalSourceOffsetKind, VerticalSourceOffsetKind)
    VELORA_PARALLAX_PROPERTY(bool, isHorizontalShiftClamped, IsHorizontalShiftClamped)
    VELORA_PARALLAX_PROPERTY(bool, isVerticalShiftClamped, IsVerticalShiftClamped)
#undef VELORA_PARALLAX_PROPERTY
    void refreshAutomaticHorizontalOffsets();
    void refreshAutomaticVerticalOffsets();
    f64 horizontalTranslation() const noexcept;
    f64 verticalTranslation() const noexcept;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;

  protected:
    std::string_view contentTemplateUri() const override;
    const PropertyDescriptor& hostedContentProperty() const override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    static const PropertyDescriptor& property(std::string_view);
    void bindSource();
    void clearSourceBinding();
    void requestLayout();
    f64 axisTranslation(bool horizontal, Size viewport) const;
    std::weak_ptr<UIElement> m_source;
    u64 m_sourceToken = 0;
    EventSubscription m_viewChanged;
    f64 m_horizontalTranslation = 0, m_verticalTranslation = 0;
};
} // namespace velora
