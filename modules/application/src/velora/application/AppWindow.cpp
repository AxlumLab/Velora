#include "velora/application/AppWindow.hpp"

#include <utility>

#if defined(_WIN32)
#include <velora/platform/windowing/NativeWindow.hpp>
#endif

namespace velora
{

#if defined(_WIN32)

struct AppWindow::Impl
{
    NativeWindow window;
    EventHandler handler;
    std::string error;

    f32 toDips(int pixels) const
    {
        return static_cast<f32>(pixels) / window.dpiScale();
    }

    void sizeOf(AppEvent& event) const
    {
        event.pixelWidth = window.width();
        event.pixelHeight = window.height();
        event.width = toDips(event.pixelWidth);
        event.height = toDips(event.pixelHeight);
        event.dpiScale = window.dpiScale();
    }

    static void onWindowEvent(void* context, WindowEvent& source)
    {
        auto& self = *static_cast<Impl*>(context);
        AppEvent event;
        event.code = source.code;
        event.modifiers = source.modifiers;
        event.x = self.toDips(source.x);
        event.y = self.toDips(source.y);
        event.wheel = source.wheel;
        switch (source.kind)
        {
        case WindowEventKind::kResized:
            event.kind = AppEventKind::kResized;
            break;
        case WindowEventKind::kDpiChanged:
            event.kind = AppEventKind::kDpiChanged;
            break;
        case WindowEventKind::kMinimized:
            event.kind = AppEventKind::kMinimized;
            break;
        case WindowEventKind::kRestored:
            event.kind = AppEventKind::kRestored;
            break;
        case WindowEventKind::kShown:
            event.kind = AppEventKind::kShown;
            break;
        case WindowEventKind::kHidden:
            event.kind = AppEventKind::kHidden;
            break;
        case WindowEventKind::kFocusGained:
            event.kind = AppEventKind::kFocusGained;
            break;
        case WindowEventKind::kFocusLost:
            event.kind = AppEventKind::kFocusLost;
            break;
        case WindowEventKind::kCloseRequested:
            event.kind = AppEventKind::kCloseRequested;
            break;
        case WindowEventKind::kClosed:
            event.kind = AppEventKind::kClosed;
            break;
        case WindowEventKind::kKeyDown:
            event.kind = AppEventKind::kKeyDown;
            break;
        case WindowEventKind::kKeyUp:
            event.kind = AppEventKind::kKeyUp;
            break;
        case WindowEventKind::kMouseMove:
            event.kind = AppEventKind::kPointerMove;
            break;
        case WindowEventKind::kMouseButtonDown:
            event.kind = AppEventKind::kPointerDown;
            break;
        case WindowEventKind::kMouseButtonUp:
            event.kind = AppEventKind::kPointerUp;
            break;
        case WindowEventKind::kMouseWheel:
            event.kind = AppEventKind::kWheel;
            break;
        case WindowEventKind::kFileDrop:
            event.kind = AppEventKind::kFileDrop;
            event.paths = source.paths;
            break;
        }
        self.sizeOf(event);
        if (self.handler)
            self.handler(event);
        source.cancel = event.cancel;
    }

    static void onTextInput(void* context, const TextInputEvent& source)
    {
        auto& self = *static_cast<Impl*>(context);
        AppEvent event;
        event.kind =
            source.kind == TextInputKind::kCommit ? AppEventKind::kTextCommit : AppEventKind::kTextPreedit;
        event.text = source.text;
        event.textCursor = source.cursor;
        event.textSelection = source.selectionLength;
        self.sizeOf(event);
        if (self.handler)
            self.handler(event);
    }
};

AppWindow::AppWindow() : m_impl(std::make_unique<Impl>())
{
    m_impl->window.setEventSink({m_impl.get(), &Impl::onWindowEvent, &Impl::onTextInput});
}

AppWindow::~AppWindow() = default;

bool AppWindow::create(const WindowOptions& options)
{
    WindowDesc desc;
    desc.title = options.title;
    desc.width = static_cast<int>(options.width + 0.5f);
    desc.height = static_cast<int>(options.height + 0.5f);
    desc.visible = options.isVisible;
    desc.resizable = options.isResizable;
    if (!m_impl->window.create(desc))
    {
        m_impl->error = m_impl->window.lastError();
        return false;
    }
    m_impl->error.clear();
    return true;
}

const std::string& AppWindow::lastError() const noexcept
{
    return m_impl->error;
}

void AppWindow::setEventHandler(EventHandler handler)
{
    m_impl->handler = std::move(handler);
}

void AppWindow::show()
{
    m_impl->window.show();
}

void AppWindow::hide()
{
    m_impl->window.hide();
}

void AppWindow::setTitle(std::string_view title)
{
    m_impl->window.setTitle(title);
}

void AppWindow::resize(f32 width, f32 height)
{
    const f32 scale = m_impl->window.dpiScale();
    m_impl->window.resize(static_cast<int>(width * scale + 0.5f), static_cast<int>(height * scale + 0.5f));
}

void AppWindow::requestClose()
{
    m_impl->window.requestClose();
}

void AppWindow::close() noexcept
{
    m_impl->window.close();
}

void AppWindow::startTextInput()
{
    m_impl->window.startTextInput();
}

void AppWindow::stopTextInput()
{
    m_impl->window.stopTextInput();
}

bool AppWindow::isTextInputActive() const noexcept
{
    return m_impl->window.textInputActive();
}

void AppWindow::setTextInputRect(f32 x, f32 y, f32 width, f32 height)
{
    const f32 scale = m_impl->window.dpiScale();
    m_impl->window.setTextInputRect(static_cast<int>(x * scale), static_cast<int>(y * scale),
                                    static_cast<int>(width * scale + 0.5f),
                                    static_cast<int>(height * scale + 0.5f));
}

void AppWindow::setCursor(AppCursor cursor)
{
    m_impl->window.setCursor(static_cast<CursorKind>(cursor));
}

bool AppWindow::isClosed() const noexcept
{
    return m_impl->window.closed();
}

bool AppWindow::isVisible() const noexcept
{
    return m_impl->window.visible();
}

bool AppWindow::isMinimized() const noexcept
{
    return m_impl->window.minimized();
}

f32 AppWindow::getWidth() const noexcept
{
    return m_impl->toDips(m_impl->window.width());
}

f32 AppWindow::getHeight() const noexcept
{
    return m_impl->toDips(m_impl->window.height());
}

i32 AppWindow::getPixelWidth() const noexcept
{
    return m_impl->window.width();
}

i32 AppWindow::getPixelHeight() const noexcept
{
    return m_impl->window.height();
}

f32 AppWindow::getDpiScale() const noexcept
{
    return m_impl->window.dpiScale();
}

void* AppWindow::getNativeHandle() const noexcept
{
    return m_impl->window.nativeHandle();
}

#else // Windows 10 is the only native backend; elsewhere windows cannot be created.

struct AppWindow::Impl
{
    std::string error = "no native window backend on this platform";
};

AppWindow::AppWindow() : m_impl(std::make_unique<Impl>()) {}
AppWindow::~AppWindow() = default;
bool AppWindow::create(const WindowOptions&)
{
    return false;
}
const std::string& AppWindow::lastError() const noexcept
{
    return m_impl->error;
}
void AppWindow::setEventHandler(EventHandler) {}
void AppWindow::show() {}
void AppWindow::hide() {}
void AppWindow::setTitle(std::string_view) {}
void AppWindow::resize(f32, f32) {}
void AppWindow::requestClose() {}
void AppWindow::close() noexcept {}
void AppWindow::startTextInput() {}
void AppWindow::stopTextInput() {}
bool AppWindow::isTextInputActive() const noexcept
{
    return false;
}
void AppWindow::setTextInputRect(f32, f32, f32, f32) {}
void AppWindow::setCursor(AppCursor) {}
bool AppWindow::isClosed() const noexcept
{
    return true;
}
bool AppWindow::isVisible() const noexcept
{
    return false;
}
bool AppWindow::isMinimized() const noexcept
{
    return false;
}
f32 AppWindow::getWidth() const noexcept
{
    return 0;
}
f32 AppWindow::getHeight() const noexcept
{
    return 0;
}
i32 AppWindow::getPixelWidth() const noexcept
{
    return 0;
}
i32 AppWindow::getPixelHeight() const noexcept
{
    return 0;
}
f32 AppWindow::getDpiScale() const noexcept
{
    return 1.0f;
}
void* AppWindow::getNativeHandle() const noexcept
{
    return nullptr;
}

#endif

} // namespace velora
