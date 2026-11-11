// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// ScrollTo/ScrollBy offset semantics adapted from WinUI 2.3 Scroller.cpp.
#include <cmath>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/Scroller.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kType = "Velora.Fluent.Scroller";
}
Scroller::Scroller()
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.name.starts_with("Scroller."))
                syncModes();
        });
}
std::string Scroller::className() const
{
    return std::string(kType);
}
const XamlTypeDeclaration& Scroller::declaration()
{
    return fluentDeclaration(kType);
}
std::string Scroller::registerType(Framework& ui)
{
    return registerFluentControl(ui, kType, [] { return createElement<Scroller>(); });
}
std::string_view Scroller::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/ScrollerView.vxbf";
}
bool Scroller::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    for (const auto& d : declaration().properties)
        if (p.id == d.id())
            return value.isString() && (value.asString() == "Enabled" || value.asString() == "Disabled");
    return ContentElement::validatePropertyValue(p, value);
}
ElementId Scroller::scrollViewer() const noexcept
{
    return contentHost();
}
f64 Scroller::result(const PropertyDescriptor* p) const
{
    auto* ui = attachedFramework();
    return ui && scrollViewer() ? ui->tree().property(scrollViewer(), *p).asDouble() : 0;
}
#define VELORA_SCROLL_RESULT(Name, Prop)                                                                     \
    f64 Scroller::Name() const                                                                               \
    {                                                                                                        \
        return result(elementProperties().props.Prop);                                                       \
    }
VELORA_SCROLL_RESULT(horizontalOffset, HorizontalOffset)
VELORA_SCROLL_RESULT(verticalOffset, VerticalOffset)
VELORA_SCROLL_RESULT(extentWidth, ExtentWidth)
VELORA_SCROLL_RESULT(extentHeight, ExtentHeight)
VELORA_SCROLL_RESULT(viewportWidth, ViewportWidth)
VELORA_SCROLL_RESULT(viewportHeight, ViewportHeight)
#undef VELORA_SCROLL_RESULT
void Scroller::syncModes()
{
    auto* ui = attachedFramework();
    if (!ui || !scrollViewer())
        return;
    const auto keepAlive = ui->tree().object(ownerElement());
    ui->set(scrollViewer(), ui->props().HorizontalScrollBarVisibility,
            getValue(fluentProperty(kType, "HorizontalScrollMode")).asString() == "Enabled" ? "Auto"
                                                                                            : "Disabled");
    if (attachedFramework() == ui)
        ui->set(scrollViewer(), ui->props().VerticalScrollBarVisibility,
                getValue(fluentProperty(kType, "VerticalScrollMode")).asString() == "Enabled" ? "Auto"
                                                                                              : "Disabled");
}
void Scroller::onAttached(Framework& ui, ElementId self)
{
    ContentElement::onAttached(ui, self);
    syncModes();
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_viewChanged = ui.subscribeEvent(scrollViewer(), ui.events().ViewChanged,
                                      [weak](ElementId, RoutedEventArgs&)
                                      {
                                          if (const auto owner = weak.lock())
                                          {
                                              auto& self = static_cast<Scroller&>(*owner);
                                              const auto handlers = self.m_handlers;
                                              const auto x = self.horizontalOffset(),
                                                         y = self.verticalOffset();
                                              for (const auto& [token, handler] : handlers)
                                                  if (self.m_handlers.contains(token))
                                                      handler(x, y);
                                          }
                                      });
}
void Scroller::onDetached() noexcept
{
    m_viewChanged.reset();
    ContentElement::onDetached();
}
bool Scroller::scrollTo(f64 x, f64 y)
{
    if (!isOwnerThread())
        throw std::logic_error("Scroller belongs to its UI thread");
    if (!std::isfinite(x) || !std::isfinite(y))
        throw std::invalid_argument("Scroll offsets must be finite");
    auto* ui = attachedFramework();
    return ui && ui->changeView(scrollViewer(), x, y);
}
bool Scroller::scrollBy(f64 x, f64 y)
{
    return scrollTo(horizontalOffset() + x, verticalOffset() + y);
}
u64 Scroller::addViewChanged(std::function<void(f64, f64)> handler)
{
    if (!isOwnerThread())
        throw std::logic_error("Scroller events belong to its UI thread");
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("Scroller tokens exhausted");
    const auto token = m_nextToken++;
    m_handlers.emplace(token, std::move(handler));
    return token;
}
bool Scroller::removeViewChanged(u64 token)
{
    return isOwnerThread() && m_handlers.erase(token) != 0;
}
} // namespace velora
