// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 PullToRefresh/RefreshVisualizer: state transitions and deferred requests.
#include "FluentTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/InputContext.hpp>
#include <velora/fluent/RefreshVisualizer.hpp>
namespace velora
{
struct RefreshRequest
{
    std::thread::id thread = std::this_thread::get_id();
    usize pending = 1; // Dispatch itself holds a deferral until every subscriber has run.
    bool dispatching = true, cancelled = false;
    std::function<void()> completed;
    void release()
    {
        if (thread != std::this_thread::get_id())
            std::terminate();
        if (cancelled || !pending)
            return;
        if (--pending == 0 && completed)
        {
            auto callback = std::move(completed);
            callback();
        }
    }
};
RefreshDeferral::RefreshDeferral(std::shared_ptr<RefreshRequest> request) : m_request(std::move(request))
{
}
RefreshDeferral::~RefreshDeferral()
{
    complete();
}
RefreshDeferral::RefreshDeferral(RefreshDeferral&& other) noexcept = default;
RefreshDeferral& RefreshDeferral::operator=(RefreshDeferral&& other) noexcept
{
    if (this != &other)
    {
        complete();
        m_request = std::move(other.m_request);
    }
    return *this;
}
void RefreshDeferral::complete()
{
    if (auto request = std::exchange(m_request, {}))
        request->release();
}
RefreshRequestedEventArgs::RefreshRequestedEventArgs(std::shared_ptr<RefreshRequest> request)
    : m_request(std::move(request))
{
}
RefreshDeferral RefreshRequestedEventArgs::getDeferral() const
{
    if (m_request->thread != std::this_thread::get_id() || !m_request->dispatching || m_request->cancelled)
        throw std::logic_error("Refresh deferrals must be acquired during UI-thread event dispatch");
    ++m_request->pending;
    return RefreshDeferral(m_request);
}
const XamlTypeDeclaration& RefreshVisualizer::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.RefreshVisualizer");
}
std::string RefreshVisualizer::registerType(Framework& ui)
{
    return ui.registerSourceControl(declaration(), [] { return createElement<RefreshVisualizer>(); }, {});
}
RefreshVisualizer::RefreshVisualizer()
{
    const auto indicator = createElement(ElementKind::kTextBlock);
    const auto& p = elementProperties().props;
    indicator->setValue(*p.Text, "\xEE\x9C\xAC"); // Segoe MDL2 Assets Refresh (E72C).
    indicator->setValue(*p.FontFamily, "Segoe MDL2 Assets");
    indicator->setValue(*p.FontSize, 20.0);
    indicator->setValue(*p.HorizontalAlignment, "Center");
    indicator->setValue(*p.VerticalAlignment, "Center");
    setContent(indicator);
    m_defaultIndicator = indicator;
}
RefreshVisualizer::~RefreshVisualizer()
{
    if (!isOwnerThread())
        std::terminate();
    cancelRequest();
}
void RefreshVisualizer::checkThread() const
{
    if (!isOwnerThread())
        throw std::logic_error("RefreshVisualizer belongs to its UI thread");
}
std::string RefreshVisualizer::className() const
{
    return "Velora.Fluent.RefreshVisualizer";
}
std::string_view RefreshVisualizer::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/RefreshVisualizerView.vxbf";
}
RefreshVisualizerState RefreshVisualizer::state() const noexcept
{
    return m_state;
}
void RefreshVisualizer::changeState(RefreshVisualizerState value)
{
    if (m_state == value)
        return;
    const auto keepAlive = weak_from_this().lock();
    const RefreshStateChangedEventArgs args{m_state, value};
    m_state = value;
    if (const auto indicator = m_defaultIndicator.lock())
        indicator->setValue(*elementProperties().props.Foreground,
                            value == RefreshVisualizerState::kIdle ? i64{0xFF666666} : i64{0xFF0067C0});
    const auto handlers = m_changed;
    for (const auto& [token, handler] : handlers)
    {
        if (m_state != value)
            break; // Reentrant state change supersedes this notification.
        if (m_changed.contains(token))
            handler(args);
    }
}
void RefreshVisualizer::requestRefresh()
{
    checkThread();
    if (m_state == RefreshVisualizerState::kRefreshing)
        return;
    const auto keepAlive = weak_from_this().lock();
    if (!keepAlive)
        throw std::logic_error("RefreshVisualizer requires shared ownership");
    auto request = std::make_shared<RefreshRequest>();
    m_request = request;
    const auto weak = weak_from_this();
    request->completed = [weak, identity = request.get()]
    {
        if (const auto object = weak.lock())
        {
            auto& self = static_cast<RefreshVisualizer&>(*object);
            if (self.m_request.get() != identity)
                return;
            self.m_request.reset();
            self.changeState(RefreshVisualizerState::kIdle);
        }
    };
    try
    {
        changeState(RefreshVisualizerState::kRefreshing);
        const RefreshRequestedEventArgs args(request);
        const auto handlers = m_requested;
        for (const auto& [token, handler] : handlers)
        {
            if (request->cancelled)
                break;
            if (m_requested.contains(token))
                handler(args);
        }
        request->dispatching = false;
        request->release();
    }
    catch (...)
    {
        request->dispatching = false;
        if (m_request == request)
        {
            cancelRequest();
            changeState(RefreshVisualizerState::kIdle);
        }
        throw;
    }
}
void RefreshVisualizer::cancelRequest() noexcept
{
    if (auto request = std::exchange(m_request, {}))
    {
        request->cancelled = true;
        request->completed = {};
    }
}
void RefreshVisualizer::onDetached() noexcept
{
    cancelRequest();
    m_dragging = false;
    m_ratio = 0;
    m_state = RefreshVisualizerState::kIdle;
    ContentElement::onDetached();
}
u64 RefreshVisualizer::addRefreshRequested(std::function<void(const RefreshRequestedEventArgs&)> handler)
{
    checkThread();
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("Refresh event tokens exhausted");
    const auto token = m_nextToken++;
    m_requested.emplace(token, std::move(handler));
    return token;
}
bool RefreshVisualizer::removeRefreshRequested(u64 token)
{
    return isOwnerThread() && m_requested.erase(token) != 0;
}
u64 RefreshVisualizer::addRefreshStateChanged(
    std::function<void(const RefreshStateChangedEventArgs&)> handler)
{
    checkThread();
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("Refresh event tokens exhausted");
    const auto token = m_nextToken++;
    m_changed.emplace(token, std::move(handler));
    return token;
}
bool RefreshVisualizer::removeRefreshStateChanged(u64 token)
{
    return isOwnerThread() && m_changed.erase(token) != 0;
}
void RefreshVisualizer::setPullDirection(RefreshPullDirection value)
{
    checkThread();
    if (value < RefreshPullDirection::kLeftToRight || value > RefreshPullDirection::kBottomToTop)
        throw std::invalid_argument("Unknown refresh pull direction");
    m_direction = value;
    m_dragging = false;
    m_ratio = 0;
    if (m_state != RefreshVisualizerState::kRefreshing)
        changeState(RefreshVisualizerState::kIdle);
}
void RefreshVisualizer::updateInteraction(f64 ratio, bool interacting)
{
    checkThread();
    if (!std::isfinite(ratio) || ratio < 0)
        throw std::invalid_argument("Refresh interaction ratio must be finite and nonnegative");
    if (m_state == RefreshVisualizerState::kRefreshing)
        return;
    const bool wasAtZero = m_ratio == 0;
    m_ratio = ratio;
    if (!interacting)
        changeState(ratio > 0 ? RefreshVisualizerState::kPeeking : RefreshVisualizerState::kIdle);
    else if (m_state == RefreshVisualizerState::kIdle && !wasAtZero && ratio > 0)
        changeState(RefreshVisualizerState::kPeeking);
    else if (m_state != RefreshVisualizerState::kPeeking)
        changeState(ratio > 0.8 ? RefreshVisualizerState::kPending
                    : ratio > 0 ? RefreshVisualizerState::kInteracting
                                : RefreshVisualizerState::kIdle);
}
void RefreshVisualizer::endInteraction()
{
    checkThread();
    m_ratio = 0;
    if (m_state == RefreshVisualizerState::kPending)
        requestRefresh();
    else if (m_state != RefreshVisualizerState::kRefreshing)
        changeState(RefreshVisualizerState::kIdle);
}
f64 RefreshVisualizer::pullDistance(const PointerEventArgs& args) const
{
    switch (m_direction)
    {
    case RefreshPullDirection::kLeftToRight:
        return args.windowX - m_startX;
    case RefreshPullDirection::kRightToLeft:
        return m_startX - args.windowX;
    case RefreshPullDirection::kBottomToTop:
        return m_startY - args.windowY;
    default:
        return args.windowY - m_startY;
    }
}
void RefreshVisualizer::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    if (m_state == RefreshVisualizerState::kRefreshing || !context.capturePointer())
        return;
    m_startX = args.windowX;
    m_startY = args.windowY;
    m_ratio = 0;
    m_dragging = true;
    context.focus();
    args.handled = true;
}
void RefreshVisualizer::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging)
        return;
    if (!context.hasPointerCapture())
    {
        m_dragging = false;
        m_ratio = 0;
        if (m_state != RefreshVisualizerState::kRefreshing)
            changeState(RefreshVisualizerState::kIdle);
        return;
    }
    updateInteraction(std::max(0.0, pullDistance(args)) / 100.0, true);
    args.handled = true;
}
void RefreshVisualizer::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging || !context.hasPointerCapture())
        return;
    m_dragging = false;
    context.releasePointer();
    updateInteraction(std::max(0.0, pullDistance(args)) / 100.0, true);
    endInteraction();
    args.handled = true;
}
void RefreshVisualizer::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kEnter || args.key == Key::kSpace)
    {
        args.handled = true;
        requestRefresh();
    }
}
bool RefreshVisualizer::isFocusable() const noexcept
{
    return true;
}
std::string RefreshVisualizer::automationName() const
{
    return m_state == RefreshVisualizerState::kRefreshing ? "Refreshing" : "Refresh";
}
} // namespace velora
