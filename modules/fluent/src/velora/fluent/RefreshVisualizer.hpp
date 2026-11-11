// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <velora/controls/ContentElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
namespace velora
{
enum class RefreshVisualizerState
{
    kIdle,
    kPeeking,
    kInteracting,
    kPending,
    kRefreshing
};
enum class RefreshPullDirection
{
    kLeftToRight,
    kTopToBottom,
    kRightToLeft,
    kBottomToTop
};
struct RefreshRequest;
// UI-thread-only, move-only work deferral; destruction also completes it.
class RefreshDeferral final
{
  public:
    RefreshDeferral() = default;
    ~RefreshDeferral();
    RefreshDeferral(RefreshDeferral&&) noexcept;
    RefreshDeferral& operator=(RefreshDeferral&&) noexcept;
    RefreshDeferral(const RefreshDeferral&) = delete;
    RefreshDeferral& operator=(const RefreshDeferral&) = delete;
    void complete();

  private:
    friend class RefreshRequestedEventArgs;
    explicit RefreshDeferral(std::shared_ptr<RefreshRequest>);
    std::shared_ptr<RefreshRequest> m_request;
};
class RefreshRequestedEventArgs final
{
  public:
    RefreshDeferral getDeferral() const;

  private:
    friend class RefreshVisualizer;
    explicit RefreshRequestedEventArgs(std::shared_ptr<RefreshRequest>);
    std::shared_ptr<RefreshRequest> m_request;
};
struct RefreshStateChangedEventArgs
{
    RefreshVisualizerState oldState, newState;
};
class RefreshVisualizer final : public ContentElement, public std::enable_shared_from_this<RefreshVisualizer>
{
  public:
    RefreshVisualizer();
    ~RefreshVisualizer() override;
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    RefreshVisualizerState state() const noexcept;
    void requestRefresh();
    u64 addRefreshRequested(std::function<void(const RefreshRequestedEventArgs&)>);
    bool removeRefreshRequested(u64);
    u64 addRefreshStateChanged(std::function<void(const RefreshStateChangedEventArgs&)>);
    bool removeRefreshStateChanged(u64);
    // Velora adapter entry: ratio is distance / visualizer extent; execute above 0.8.
    void updateInteraction(f64 ratio, bool interactingForRefresh);
    void endInteraction();
    void setPullDirection(RefreshPullDirection);
    void onDetached() noexcept override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;
    std::string automationName() const override;

  protected:
    std::string_view contentTemplateUri() const override;

  private:
    void changeState(RefreshVisualizerState);
    void cancelRequest() noexcept;
    void checkThread() const;
    f64 pullDistance(const PointerEventArgs&) const;
    RefreshVisualizerState m_state = RefreshVisualizerState::kIdle;
    RefreshPullDirection m_direction = RefreshPullDirection::kTopToBottom;
    f64 m_ratio = 0;
    f32 m_startX = 0, m_startY = 0;
    bool m_dragging = false;
    std::weak_ptr<UIElement> m_defaultIndicator;
    std::shared_ptr<RefreshRequest> m_request;
    std::map<u64, std::function<void(const RefreshRequestedEventArgs&)>> m_requested;
    std::map<u64, std::function<void(const RefreshStateChangedEventArgs&)>> m_changed;
    u64 m_nextToken = 1;
};
} // namespace velora
