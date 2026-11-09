#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <velora/composition/VisualId.hpp>
#include <velora/globals.hpp>
#include <velora/graphics/DisplayList.hpp>

// GPU-independent composition layer. It owns a Visual tree that is separate from
// ElementTree and layout, and lowers it to a DisplayList snapshot.
// All mutation and snapshotting happens on the UI thread; snapshots are immutable
// and may be handed to the thread that owns the GPU context.
namespace velora
{

struct VisualClip
{
    enum class Kind : u8
    {
        kNone,
        kRect,
        kRoundedRect
    };
    Kind kind = Kind::kNone;
    Rect bounds{};
    float radius = 0;
};

// Visual effects. radius is the Gaussian sigma in local pixels. DropShadow draws the
// visual's content and children tinted with `color` (its alpha is the shadow opacity),
// blurred and moved by `offset`, beneath the visual. Blur blurs the visual itself.
// Both are clipped by the visual's own clip.
struct Effect
{
    enum class Kind : u8
    {
        kDropShadow,
        kBlur
    };
    Kind kind = Kind::kDropShadow;
    float radius = 0;
    Point offset{};
    Color color{};
};

struct FrameStats
{
    std::size_t visualsLowered = 0; // visuals traversed outside reused layer fragments
    std::size_t layerHits = 0;
    std::size_t layerMisses = 0;
    std::size_t effectsApplied = 0;
    std::size_t commands = 0;
};

struct SceneSnapshot
{
    u64 frame = 0;
    float width = 0, height = 0; // DIPs
    float dpi = 1;               // device pixels per DIP; the display list is in DIPs
    std::shared_ptr<const DisplayList> displayList;
    FrameStats stats;
};

struct LayerCacheStats
{
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t budget = 0;
    u64 evictions = 0;
};

class Compositor
{
public:
    explicit Compositor(std::size_t layerCacheBudgetBytes = 8 * 1024 * 1024);

    VisualId root() const noexcept
    {
        return m_root;
    }
    VisualId createVisual(VisualId parent);
    bool remove(VisualId id);
    // Moves a visual within its parent's children (paint order before z-index sorting).
    bool reorder(VisualId id, std::size_t index);
    bool contains(VisualId id) const noexcept
    {
        return m_visuals.contains(id);
    }
    const std::vector<VisualId>& children(VisualId id) const;

    // Render properties: changing them never invalidates the visual's own layer fragment.
    bool setOffset(VisualId id, Point offset);
    bool setTransform(VisualId id, Transform transform, Point anchor = {});
    bool setOpacity(VisualId id, float opacity);
    bool setClip(VisualId id, VisualClip clip);
    bool setZIndex(VisualId id, int zIndex);
    bool setVisible(VisualId id, bool visible);
    bool setEffects(VisualId id, std::vector<Effect> effects);
    // Content is drawn in the visual's local coordinates before its children.
    bool setContent(VisualId id, DisplayList content);
    // Cache the lowered content+children of this visual between frames.
    bool setLayer(VisualId id, bool layer);

    void setBackground(Color color);
    void resize(float width, float height, float dpi);
    bool needsFrame() const noexcept
    {
        return m_dirty;
    }
    // Lowers dirty subtrees, reuses clean layer fragments and clears dirty state.
    SceneSnapshot snapshot();

    LayerCacheStats layerCache() const noexcept;

private:
    struct Bounds
    {
        bool unbounded = false;
        std::optional<Rect> rect;
        void add(const Bounds& other);
    };
    struct Visual
    {
        VisualId parent = 0;
        std::vector<VisualId> children;
        Point offset{};
        Transform transform{};
        Point anchor{};
        float opacity = 1;
        VisualClip clip{};
        int zIndex = 0;
        bool visible = true;
        bool layer = false;
        std::vector<Effect> effects;
        std::shared_ptr<const DisplayList> content;
        Bounds contentBounds;
        bool contentDirty = true; // own content or child list changed
        bool subtreeDirty = true; // some descendant changed
    };
    // Lowered content+children of a layer visual, shared with snapshots through LayerRef.
    struct Fragment
    {
        std::shared_ptr<const CommandList> commands;
        Bounds bounds;
        u64 version = 0;
        std::size_t bytes = 0;
    };

    Visual* find(VisualId id) noexcept;
    template <class F> bool update(VisualId id, F&& apply);
    void invalidateAncestors(VisualId id);
    Bounds lower(VisualId id, std::vector<Command>& out, FrameStats& stats);
    Bounds lowerInner(Visual& visual, std::vector<Command>& out, FrameStats& stats);
    const Fragment* cached(VisualId id);
    void store(VisualId id, Fragment fragment);
    void evict(VisualId id);

    VisualId m_next = 1;
    VisualId m_root = 0;
    std::unordered_map<VisualId, Visual> m_visuals;
    Color m_background{0, 0, 0, 1};
    float m_width = 0, m_height = 0, m_dpi = 1;
    bool m_dirty = true;
    u64 m_frame = 0;
    u64 m_fragmentVersion = 0;

    std::size_t m_cacheBudget;
    std::size_t m_cacheBytes = 0;
    u64 m_cacheEvictions = 0;
    std::list<VisualId> m_cacheLru; // front = most recently used
    struct CacheEntry
    {
        Fragment fragment;
        std::list<VisualId>::iterator lru;
    };
    std::unordered_map<VisualId, CacheEntry> m_cache;
};

} // namespace velora
