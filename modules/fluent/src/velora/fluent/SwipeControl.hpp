// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <velora/controls/ContentElement.hpp>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
class SwipeControl;
enum class SwipeSide
{
    kLeft,
    kRight,
    kTop,
    kBottom
};
enum class SwipeMode
{
    kReveal,
    kExecute
};
class SwipeItem final : public FluentElement
{
  public:
    SwipeItem();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    std::string text() const;
    PropertySetResult setText(std::string);
    void setCommand(std::function<void()>);
    u64 addInvoked(std::function<void(SwipeControl&)>);
    bool removeInvoked(u64);
    void invoke(SwipeControl&);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class SwipeControl;
    std::weak_ptr<DependencyObject> m_owner;
    std::function<void()> m_command;
    std::map<u64, std::function<void(SwipeControl&)>> m_handlers;
    u64 m_nextToken = 1;
};
struct SwipeItems
{
    SwipeMode mode = SwipeMode::kReveal;
    std::vector<std::shared_ptr<SwipeItem>> items;
};
class SwipeControl final : public ContentElement
{
  public:
    SwipeControl();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    void setItems(SwipeSide, SwipeItems);
    const SwipeItems& items(SwipeSide) const;
    void open(SwipeSide);
    void close();
    bool isOpen() const noexcept;
    void invoke(SwipeSide, usize);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;

  protected:
    std::string_view contentTemplateUri() const override;
    std::string_view contentHostName() const override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void rebuild();
    std::array<SwipeItems, 4> m_groups;
    std::array<ElementId, 4> m_hosts{};
    std::vector<ElementId> m_nodes;
    std::optional<SwipeSide> m_open;
    f32 m_startX = 0, m_startY = 0;
    bool m_dragging = false, m_rebuilding = false;
};
} // namespace velora
