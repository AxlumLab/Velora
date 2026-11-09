#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{

struct WindowOptions
{
    std::string title = "Velora";
    f32 width = 960; // client size in DIPs
    f32 height = 640;
    bool isResizable = true;
    bool isVisible = true;
};

enum class AppEventKind : u8
{
    kResized,
    kDpiChanged,
    kMinimized,
    kRestored,
    kShown,
    kHidden,
    kFocusGained,
    kFocusLost,
    kCloseRequested, // set AppEvent::cancel to keep the window open
    kClosed,
    kKeyDown,
    kKeyUp,
    kPointerMove,
    kPointerDown,
    kPointerUp,
    kWheel,
    kTextPreedit,
    kTextCommit,
    kFileDrop
};

enum AppModifier : u32
{
    kAppModifierNone = 0,
    kAppModifierShift = 1,
    kAppModifierControl = 2,
    kAppModifierAlt = 4,
};

// A window event in DIPs. Pixel sizes are reported alongside for the graphics surface.
struct AppEvent
{
    AppEventKind kind = AppEventKind::kClosed;
    f32 x = 0; // pointer position in DIPs
    f32 y = 0;
    f32 width = 0; // client size in DIPs (kResized, kDpiChanged)
    f32 height = 0;
    i32 pixelWidth = 0; // client size in physical pixels
    i32 pixelHeight = 0;
    u32 code = 0; // virtual key, or mouse button (1 left, 2 right, 3 middle)
    u32 modifiers = kAppModifierNone;
    f32 dpiScale = 1.0f;
    f32 wheel = 0.0f; // notches; positive away from the user
    std::string text;
    i32 textCursor = 0; // preedit caret, UTF-8 bytes into text
    i32 textSelection = 0;
    std::vector<std::string> paths;
    bool cancel = false;
};

enum class AppCursor : u8
{
    kArrow,
    kIBeam,
    kHand,
    kWait,
    kSizeAll
};

// A native top-level window. Events arrive on the UI thread, in order, while the application
// runs (also during interactive move/size); the handler sees DIPs.
class AppWindow final
{
public:
    using EventHandler = std::function<void(AppEvent&)>;

    AppWindow();
    ~AppWindow();
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    [[nodiscard]] bool create(const WindowOptions& options);
    [[nodiscard]] const std::string& lastError() const noexcept;
    void setEventHandler(EventHandler handler);

    void show();
    void hide();
    void setTitle(std::string_view title);
    void resize(f32 width, f32 height); // DIPs
    // Raises kCloseRequested; closes unless the handler cancels.
    void requestClose();
    // Closes without asking.
    void close() noexcept;

    void startTextInput();
    void stopTextInput();
    [[nodiscard]] bool isTextInputActive() const noexcept;
    // Caret rectangle in DIPs, for the IME composition and candidate windows.
    void setTextInputRect(f32 x, f32 y, f32 width, f32 height);
    void setCursor(AppCursor cursor);

    [[nodiscard]] bool isClosed() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;
    [[nodiscard]] f32 getWidth() const noexcept; // client size in DIPs
    [[nodiscard]] f32 getHeight() const noexcept;
    [[nodiscard]] i32 getPixelWidth() const noexcept;
    [[nodiscard]] i32 getPixelHeight() const noexcept;
    [[nodiscard]] f32 getDpiScale() const noexcept;
    [[nodiscard]] void* getNativeHandle() const noexcept; // HWND on Windows

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace velora
