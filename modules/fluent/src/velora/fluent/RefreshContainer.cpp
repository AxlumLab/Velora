// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 PullToRefresh/RefreshContainer: visualizer ownership and request forwarding.
#include "FluentTypes.hpp"
#include <array>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/RefreshContainer.hpp>
namespace velora
{
namespace
{
constexpr std::array<std::string_view, 4> kDirections{"LeftToRight", "TopToBottom", "RightToLeft",
                                                      "BottomToTop"};
}
const XamlTypeDeclaration& RefreshContainer::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.RefreshContainer");
}
const PropertyDescriptor& RefreshContainer::pullDirectionProperty()
{
    static PropertyRegistry registry;
    static const auto& p =
        registry.define("RefreshContainer.PullDirection", "TopToBottom", PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& RefreshContainer::visualizerProperty()
{
    static PropertyRegistry registry;
    static const auto& p = registry.define("RefreshContainer.Visualizer", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
std::string RefreshContainer::registerType(Framework& ui)
{
    const std::array properties{&pullDirectionProperty()};
    return ui.registerSourceControl(
        declaration(), [] { return createElement<RefreshContainer>(); }, properties);
}
RefreshContainer::RefreshContainer()
{
    setVisualizer(createElement<RefreshVisualizer>());
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == visualizerProperty().id)
                syncVisualizer();
            if (p.id == pullDirectionProperty().id)
                syncDirection();
        });
}
RefreshContainer::~RefreshContainer()
{
    if (!isOwnerThread())
        std::terminate();
    unbindVisualizer();
}
std::string RefreshContainer::className() const
{
    return "Velora.Fluent.RefreshContainer";
}
std::string_view RefreshContainer::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/RefreshContainerView.vxbf";
}
std::string_view RefreshContainer::contentHostName() const
{
    return "ContentHost";
}
RefreshPullDirection RefreshContainer::pullDirection() const
{
    const auto value = getValue(pullDirectionProperty()).asString();
    for (usize i = 0; i < kDirections.size(); ++i)
        if (value == kDirections[i])
            return static_cast<RefreshPullDirection>(i);
    return RefreshPullDirection::kTopToBottom;
}
PropertySetResult RefreshContainer::setPullDirection(RefreshPullDirection value)
{
    if (value < RefreshPullDirection::kLeftToRight || value > RefreshPullDirection::kBottomToTop)
        throw std::invalid_argument("Unknown refresh pull direction");
    return setValue(pullDirectionProperty(), std::string(kDirections[static_cast<usize>(value)]));
}
std::shared_ptr<RefreshVisualizer> RefreshContainer::visualizer() const
{
    const auto& ref = getValue(visualizerProperty()).asObject();
    return {ref.owner, static_cast<RefreshVisualizer*>(ref.object)};
}
PropertySetResult RefreshContainer::setVisualizer(std::shared_ptr<RefreshVisualizer> value)
{
    if (!value)
        value = createElement<RefreshVisualizer>();
    return setValue(visualizerProperty(), ObjectRef::share<RefreshVisualizer>(std::move(value)));
}
bool RefreshContainer::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == pullDirectionProperty().id)
    {
        if (!v.isString())
            return false;
        for (const auto direction : kDirections)
            if (v.asString() == direction)
                return true;
        return false;
    }
    if (p.id == visualizerProperty().id)
    {
        if (m_syncing || !v.isObject() || !v.asObject())
            return false;
        const auto& ref = v.asObject();
        if (!ref.owner || ref.owner.get() != ref.object || !ref.type ||
            *ref.type != typeid(RefreshVisualizer))
            return false;
        const auto* item = static_cast<RefreshVisualizer*>(ref.object);
        if (const auto hosted = content();
            hosted && (hosted->containsOwnedContent(*item) || item->containsOwnedContent(*hosted)))
            return false;
        return item->isOwnerThread() && !item->containsOwnedContent(*this) &&
               (!item->isInTree() ||
                (attachedFramework() && attachedFramework()->tree().element(*item) == m_visualizerNode));
    }
    if (p.id == elementProperties().props.Content->id && v.isObject() && v.asObject() && v.asObject().type &&
        *v.asObject().type == typeid(UIElement))
    {
        const auto* item = static_cast<UIElement*>(v.asObject().object);
        if (const auto indicator = visualizer();
            indicator && (item->containsOwnedContent(*indicator) || indicator->containsOwnedContent(*item)))
            return false;
    }
    return ContentElement::validatePropertyValue(p, v);
}
std::vector<std::shared_ptr<UIElement>> RefreshContainer::ownedContentObjects() const
{
    auto result = ContentElement::ownedContentObjects();
    if (auto item = visualizer())
        result.push_back(std::move(item));
    return result;
}
void RefreshContainer::unbindVisualizer() noexcept
{
    if (auto item = m_bound.lock())
        item->removeRefreshRequested(m_forwardToken);
    m_bound.reset();
    m_forwardToken = 0;
}
void RefreshContainer::syncVisualizer()
{
    if (m_syncing)
        return;
    m_syncing = true;
    struct Reset
    {
        bool& value;
        ~Reset()
        {
            value = false;
        }
    } reset{m_syncing};
    const auto item = visualizer();
    if (m_bound.lock() == item)
        return;
    unbindVisualizer();
    if (auto* ui = attachedFramework(); ui && m_visualizerNode)
    {
        const auto old = std::exchange(m_visualizerNode, 0);
        ui->tree().remove(old);
    }
    item->setPullDirection(pullDirection());
    const auto weak = weak_from_this();
    if (weak.expired())
        return; // Off-tree constructor; bind after shared ownership or on first request.
    m_forwardToken = item->addRefreshRequested(
        [weak](const RefreshRequestedEventArgs& args)
        {
            if (const auto self = weak.lock())
            {
                const auto handlers = self->m_requested;
                const auto source = self->visualizer();
                for (const auto& [token, handler] : handlers)
                {
                    if (source != self->visualizer() ||
                        source->state() != RefreshVisualizerState::kRefreshing)
                        break;
                    if (self->m_requested.contains(token))
                        handler(args);
                }
            }
        });
    m_bound = item;
    if (auto* ui = attachedFramework())
    {
        const auto root = ui->tree().parent(contentHost());
        m_visualizerNode = ui->insert(item, root);
        if (!m_visualizerNode)
            throw std::runtime_error("Refresh visualizer failed to attach");
        syncDirection();
    }
}
void RefreshContainer::syncDirection()
{
    const auto item = visualizer();
    const auto direction = pullDirection();
    item->setPullDirection(direction);
    auto* ui = attachedFramework();
    if (!ui || !m_visualizerNode)
        return;
    const auto root = ui->tree().parent(contentHost());
    const auto& p = ui->props();
    const bool vertical =
        direction == RefreshPullDirection::kTopToBottom || direction == RefreshPullDirection::kBottomToTop;
    const bool far =
        direction == RefreshPullDirection::kBottomToTop || direction == RefreshPullDirection::kRightToLeft;
    GridLengths tracks{{GridLength::Unit::kPixel, 40}, {GridLength::Unit::kStar, 1}};
    if (far)
        std::swap(tracks[0], tracks[1]);
    const GridLengths single{{GridLength::Unit::kStar, 1}};
    ui->set(root, p.RowDefinitions, vertical ? tracks : single);
    ui->set(root, p.ColumnDefinitions, vertical ? single : tracks);
    ui->set(m_visualizerNode, p.GridRow, i64{vertical && far ? 1 : 0});
    ui->set(m_visualizerNode, p.GridColumn, i64{!vertical && far ? 1 : 0});
    ui->set(contentHost(), p.GridRow, i64{vertical && !far ? 1 : 0});
    ui->set(contentHost(), p.GridColumn, i64{!vertical && !far ? 1 : 0});
}
void RefreshContainer::onAttached(Framework& ui, ElementId self)
{
    unbindVisualizer(); // A programmatic request may have bound the off-tree instance.
    ContentElement::onAttached(ui, self);
    syncVisualizer();
}
void RefreshContainer::onDetached() noexcept
{
    unbindVisualizer();
    m_visualizerNode = 0;
    ContentElement::onDetached();
}
void RefreshContainer::requestRefresh()
{
    if (!isOwnerThread())
        throw std::logic_error("RefreshContainer belongs to its UI thread");
    const auto keepAlive = shared_from_this();
    syncVisualizer();
    const auto item = visualizer();
    item->requestRefresh();
}
u64 RefreshContainer::addRefreshRequested(std::function<void(const RefreshRequestedEventArgs&)> handler)
{
    if (!isOwnerThread())
        throw std::logic_error("RefreshContainer events belong to its UI thread");
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("Refresh event tokens exhausted");
    const auto token = m_nextToken++;
    m_requested.emplace(token, std::move(handler));
    return token;
}
bool RefreshContainer::removeRefreshRequested(u64 token)
{
    return isOwnerThread() && m_requested.erase(token) != 0;
}
} // namespace velora
