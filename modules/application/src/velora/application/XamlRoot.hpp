#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <velora/globals.hpp>
#include <velora/resources/ResourceContext.hpp>
#include <velora/resources/ResourceProvider.hpp>
#include <velora/runtime/Framework.hpp>

// XamlRoot: the ownership and state boundary of one UI tree.
//
// Everything that must not leak between windows lives here, one set per root: the composition
// tree, the UI context (layout, focus, pointer capture, hover, animations, theme) and the resource
// context (scale, theme, language). Two windows of one application share only immutable or
// thread-safe services — the graphics device, text service and type registry — never a root.
//
// Each root owns its Framework and Compositor. The Framework hosts typed UIElement objects;
// their layout, input and presentation state is local to this root.
//
// UWP members: Content, Size, RasterizationScale, IsHostVisible, Changed. UWP's
// XamlRootChangedEventArgs is empty; Velora reports which aspects changed as a Velora extension.

namespace velora
{

class Compositor;
class DependencyObject;

class XamlRoot final
{
public:
    enum Change : u32
    {
        kSizeChanged = 1,
        kRasterizationScaleChanged = 2,
        kHostVisibilityChanged = 4,
        kThemeChanged = 8,
        kContentChanged = 16,
        kLanguageChanged = 32
    };
    using ChangedHandler = std::function<void(XamlRoot&, u32 changes)>;

    // `measure` measures text for layout; the default approximates it without a font backend.
    explicit XamlRoot(TextMeasurer measure = approximateTextWidth);
    ~XamlRoot();
    XamlRoot(const XamlRoot&) = delete;
    XamlRoot& operator=(const XamlRoot&) = delete;

    // The root's UI context. Elements created here belong to this root only.
    [[nodiscard]] Framework& framework() noexcept
    {
        return *m_framework;
    }
    [[nodiscard]] Compositor& compositor() noexcept
    {
        return *m_compositor;
    }

    // ---- Content ---------------------------------------------------------------------------
    [[nodiscard]] ElementId content() const noexcept
    {
        return m_content;
    }
    // Mounts `element` (a top-level element of this root) as the content. The previous content is
    // unmounted — kept with all its state, not destroyed — and can be mounted again. 0 clears the
    // content. False for an element of another root, a nested element, or off the UI thread.
    bool setContent(ElementId element);
    // The same by object identity; refuses an object that belongs to another root (C-3.6).
    bool setContent(const DependencyObject& element);

    // The root whose mounted content contains `object`, or null (UIElement.XamlRoot): an element
    // that is unmounted or not in any root's tree has no XamlRoot. UI thread only.
    [[nodiscard]] static XamlRoot* of(const DependencyObject& object);
    // True when `element` is in this root's mounted content.
    [[nodiscard]] bool contains(ElementId element) const;

    // ---- Host state ------------------------------------------------------------------------
    [[nodiscard]] f32 width() const noexcept
    {
        return m_width;
    }
    [[nodiscard]] f32 height() const noexcept
    {
        return m_height;
    }
    [[nodiscard]] f32 rasterizationScale() const noexcept
    {
        return m_context.scale;
    }
    [[nodiscard]] bool isHostVisible() const noexcept
    {
        return m_hostVisible;
    }
    // Called by the hosting window (DIP size, device pixels per DIP, visibility).
    void setHostState(f32 width, f32 height, f32 scale, bool visible);

    // ---- Theme and resources ---------------------------------------------------------------
    [[nodiscard]] Theme theme() const noexcept
    {
        return m_framework->theme();
    }
    // The root's requested theme: re-resolves this root's ThemeResources only.
    void setTheme(Theme theme);
    void setLanguage(std::string language);
    [[nodiscard]] const ResourceContext& resourceContext() const noexcept
    {
        return m_context;
    }
    // Resolves `uri` for this root's context (scale, theme, language) through `manager`: two roots
    // on different monitors or in different languages get different candidates of one resource.
    [[nodiscard]] std::optional<ResolvedResource>
    resource(const ResourceUri& uri, const ResourceManager& manager = ResourceManager::instance()) const
    {
        return manager.resolve(uri, m_context);
    }
    [[nodiscard]] std::optional<std::string>
    string(const ResourceUri& uri, const ResourceManager& manager = ResourceManager::instance()) const
    {
        return manager.string(uri, m_context);
    }

    // Changed: tokens never repeat for this root; 0 is never a token.
    u64 addChanged(ChangedHandler handler);
    bool removeChanged(u64 token);

private:
    void raise(u32 changes);

    std::unique_ptr<Compositor> m_compositor;
    std::unique_ptr<Framework> m_framework;
    ElementId m_content = 0;
    f32 m_width = 0;
    f32 m_height = 0;
    bool m_hostVisible = false;
    ResourceContext m_context;
    std::vector<std::pair<u64, std::shared_ptr<ChangedHandler>>> m_handlers;
    u64 m_nextToken = 1;
};

} // namespace velora
