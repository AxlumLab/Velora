#include "velora/application/XamlHost.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>
#include <velora/application/NativeSink.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/composition/FrameScheduler.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/runtime/Framework.hpp>

namespace velora
{

namespace
{
// Win32 virtual keys the built-in elements handle.
std::optional<Key> keyOf(u32 code)
{
    switch (code)
    {
    case 0x25:
        return Key::kLeft; // VK_LEFT
    case 0x27:
        return Key::kRight; // VK_RIGHT
    case 0x24:
        return Key::kHome; // VK_HOME
    case 0x23:
        return Key::kEnd; // VK_END
    case 0x08:
        return Key::kBackspace; // VK_BACK
    case 0x2E:
        return Key::kDelete; // VK_DELETE
    case 0x0D:
        return Key::kEnter; // VK_RETURN
    case 0x1B:
        return Key::kEscape; // VK_ESCAPE
    case 0x20:
        return Key::kSpace;
    case 0x73:
        return Key::kF4;
    case 'A':
        return Key::kA;
    case 0x26:
        return Key::kUp; // VK_UP
    case 0x28:
        return Key::kDown; // VK_DOWN
    case 0x21:
        return Key::kPageUp; // VK_PRIOR
    case 0x22:
        return Key::kPageDown; // VK_NEXT
    case 0x09:
        return Key::kTab; // VK_TAB
    default:
        return std::nullopt;
    }
}

// Preedit carets arrive as UTF-8 byte offsets; the framework counts code points.
std::size_t codePoints(std::string_view text, i32 bytes)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size() && i < static_cast<std::size_t>(bytes); ++i)
        count += (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80;
    return count;
}
} // namespace

XamlHost::XamlHost() = default;

XamlHost::~XamlHost()
{
    detach();
}

bool XamlHost::attach(AppWindow& window, Framework& framework, Compositor& compositor,
                      GraphicsDevice& device) noexcept
{
    if (m_window || window.isClosed())
    {
        m_error = m_window ? "XamlHost is already attached" : "Cannot attach to a closed window";
        return false;
    }
    if (!device.isReady())
    {
        m_error = "GraphicsDevice is not initialized";
        return false;
    }

    // Attach the graphics surface to the window.
    auto sink = std::make_unique<NativeSink>();
    const f32 scale = window.getDpiScale();
    const SurfaceConfig config{{static_cast<u32>(std::lround(window.getWidth() * scale)),
                                static_cast<u32>(std::lround(window.getHeight() * scale))},
                               scale,
                               false};
    if (!sink->attach(device, window.getNativeHandle(), config))
    {
        m_error = sink->lastError();
        return false;
    }

    // Create the frame scheduler.
    auto scheduler = std::make_unique<FrameScheduler>(compositor, *sink);

    m_window = &window;
    m_framework = &framework;
    m_compositor = &compositor;
    m_sink = std::move(sink);
    m_scheduler = std::move(scheduler);
    m_suspended = window.isMinimized();
    window.setEventHandler([this](AppEvent& event) { dispatch(event); });
    resizeViewport();
    m_error.clear();
    return true;
}

void XamlHost::detach() noexcept
{
    if (!m_window)
        return;
    if (!m_window->isClosed())
    {
        m_window->setEventHandler({});
        m_window->stopTextInput();
    }
    if (m_root && m_framework)
        m_framework->tree().remove(m_root);
    m_root = 0;
    m_scheduler.reset();
    m_sink.reset();
    m_window = nullptr;
    m_framework = nullptr;
    m_compositor = nullptr;
}

bool XamlHost::setRoot(u64 element)
{
    if (!m_framework || !m_framework->isOwnerThread())
        return false;
    if (element && (!m_framework->tree().contains(element) || m_framework->tree().parent(element)))
        return false;
    if (m_root && m_root != element)
        m_framework->tree().remove(m_root);
    m_root = element;
    return true;
}

bool XamlHost::isAttached() const noexcept
{
    return m_window != nullptr;
}

void XamlHost::setEventHandler(AppWindow::EventHandler handler)
{
    m_handler = std::move(handler);
}

bool XamlHost::isSuspended() const noexcept
{
    return m_suspended;
}

void XamlHost::dispatch(AppEvent& event)
{
    if (!m_window)
        return;
    Framework& ui = *m_framework;
    const bool shift = (event.modifiers & kAppModifierShift) != 0;
    const bool control = (event.modifiers & kAppModifierControl) != 0;
    switch (event.kind)
    {
    case AppEventKind::kResized:
    case AppEventKind::kDpiChanged:
        resizeViewport();
        break;
    case AppEventKind::kMinimized:
        ui.closePopup(false);
        m_suspended = true;
        break;
    case AppEventKind::kFocusLost:
        ui.closePopup(false);
        break;
    case AppEventKind::kRestored:
        m_suspended = false;
        resizeViewport();
        break;
    case AppEventKind::kPointerMove:
    {
        ui.pointerMove(event.x, event.y);
        ElementId target = ui.hitTest(event.x, event.y);
        m_window->setCursor(target && ui.tree().is(target, ElementKind::kTextBox) ? AppCursor::kIBeam
                                                                                  : AppCursor::kArrow);
        break;
    }
    case AppEventKind::kPointerDown:
        if (event.code == 1)
            ui.pointerDown(event.x, event.y);
        break;
    case AppEventKind::kPointerUp:
        if (event.code == 1)
            ui.pointerUp(event.x, event.y);
        break;
    case AppEventKind::kWheel:
        ui.wheel(event.x, event.y, -event.wheel * 48.0f); // one notch scrolls three 16-DIP lines
        break;
    case AppEventKind::kKeyDown:
        if (const auto key = keyOf(event.code))
            ui.key(*key, shift, control, (event.modifiers & kAppModifierAlt) != 0);
        break;
    case AppEventKind::kTextCommit:
        ui.textInput(event.text);
        break;
    case AppEventKind::kTextPreedit:
        ui.textEditing(event.text, codePoints(event.text, event.textCursor));
        break;
    default:
        break;
    }
    syncTextInput();
    if (m_handler)
        m_handler(event);
    if (event.kind == AppEventKind::kClosed)
        detach();
}

void XamlHost::update()
{
    if (!m_window || m_suspended)
        return;
    m_framework->update(m_window->getWidth(), m_window->getHeight());
    syncTextInput();
    tick();
}

bool XamlHost::hasPendingWork() const noexcept
{
    return m_scheduler && m_scheduler->hasPendingWork();
}

const std::string& XamlHost::lastError() const noexcept
{
    return m_error;
}

void XamlHost::resizeViewport()
{
    if (m_window->getPixelWidth() <= 0 || m_window->getPixelHeight() <= 0)
        return;
    // The composition tree works in DIPs; the scale maps it onto the window's pixels.
    m_compositor->resize(m_window->getWidth(), m_window->getHeight(), m_window->getDpiScale());

    // Resize the graphics surface to match the window.
    if (m_sink)
    {
        const f32 scale = m_window->getDpiScale();
        const SurfaceConfig config{{static_cast<u32>(std::lround(m_window->getWidth() * scale)),
                                    static_cast<u32>(std::lround(m_window->getHeight() * scale))},
                                   scale,
                                   false};
        if (!m_sink->resize(config))
            m_error = m_sink->lastError();
    }

    if (!m_suspended)
        m_framework->update(m_window->getWidth(), m_window->getHeight());
}

void XamlHost::tick()
{
    if (!m_scheduler || m_suspended)
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now.time_since_epoch()).count();
    (void)m_scheduler->tick(elapsed);
}

void XamlHost::syncTextInput()
{
    if (!m_window || m_window->isClosed())
        return;
    const ElementId focused = m_framework->focused();
    const bool wantsText = focused && m_framework->tree().is(focused, ElementKind::kTextBox);
    if (wantsText)
    {
        if (!m_window->isTextInputActive())
            m_window->startTextInput();
        const Box caret = m_framework->caretBounds();
        m_window->setTextInputRect(caret.x, caret.y, caret.width, caret.height);
    }
    else if (m_window->isTextInputActive())
    {
        m_window->stopTextInput();
    }
}

} // namespace velora
