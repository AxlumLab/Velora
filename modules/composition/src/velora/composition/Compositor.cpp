#include "velora/composition/Compositor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <velora/globals.hpp>

namespace velora
{
namespace
{

// 2D affine map: (x, y) -> (a*x + c*y + tx, b*x + d*y + ty).
struct Affine
{
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Affine operator*(const Affine& r) const
    {
        return {a * r.a + c * r.b, b * r.a + d * r.b,        a * r.c + c * r.d,
                b * r.c + d * r.d, a * r.tx + c * r.ty + tx, b * r.tx + d * r.ty + ty};
    }
    Point apply(float x, float y) const
    {
        return {a * x + c * y + tx, b * x + d * y + ty};
    }
};

Affine translate(float x, float y)
{
    return {1, 0, 0, 1, x, y};
}
Affine scale(float x, float y)
{
    return {x, 0, 0, y, 0, 0};
}
Affine rotate(float degrees)
{
    const float r = degrees * std::numbers::pi_v<float> / 180.0f;
    return {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
}

Rect transformRect(const Affine& m, const Rect& r)
{
    const Point corners[] = {m.apply(r.x, r.y), m.apply(r.x + r.width, r.y), m.apply(r.x, r.y + r.height),
                             m.apply(r.x + r.width, r.y + r.height)};
    float x0 = corners[0].x, y0 = corners[0].y, x1 = x0, y1 = y0;
    for (const auto& p : corners)
    {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

Rect unite(const Rect& a, const Rect& b)
{
    const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    return {x0, y0, std::max(a.x + a.width, b.x + b.width) - x0,
            std::max(a.y + a.height, b.y + b.height) - y0};
}

Rect intersect(const Rect& a, const Rect& b)
{
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width), y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

Rect inflate(Rect r, const Paint& paint)
{
    if (!paint.stroke)
        return r;
    const float half = paint.strokeWidth / 2;
    return {r.x - half, r.y - half, r.width + paint.strokeWidth, r.height + paint.strokeWidth};
}

// Used as SaveLayer bounds when content extent is unknown; the backend clips it to the target.
constexpr Rect kUnboundedRect{-1.0e7f, -1.0e7f, 2.0e7f, 2.0e7f};

Rect inflate(Rect r, float by)
{
    return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by};
}

std::size_t estimateBytes(const std::vector<Command>& commands)
{
    std::size_t bytes = commands.capacity() * sizeof(Command);
    for (const auto& command : commands)
    {
        if (const auto* text = std::get_if<TextCommand>(&command))
            bytes += text->value.capacity() + text->fontFamily.capacity();
        else if (const auto* image = std::get_if<ImageCommand>(&command))
            bytes += image->path.capacity();
        else if (const auto* path = std::get_if<PathCommand>(&command))
            bytes += path->elements.capacity() * sizeof(PathElement);
    }
    return bytes;
}

} // namespace

void Compositor::Bounds::add(const Bounds& other)
{
    unbounded = unbounded || other.unbounded;
    if (other.rect)
        rect = rect ? unite(*rect, *other.rect) : *other.rect;
}

Compositor::Compositor(std::size_t layerCacheBudgetBytes) : m_cacheBudget(layerCacheBudgetBytes)
{
    m_root = m_next++;
    m_visuals.emplace(m_root, Visual{});
}

Compositor::Visual* Compositor::find(VisualId id) noexcept
{
    const auto it = m_visuals.find(id);
    return it == m_visuals.end() ? nullptr : &it->second;
}

const std::vector<VisualId>& Compositor::children(VisualId id) const
{
    static const std::vector<VisualId> empty;
    const auto it = m_visuals.find(id);
    return it == m_visuals.end() ? empty : it->second.children;
}

VisualId Compositor::createVisual(VisualId parent)
{
    Visual* parentVisual = find(parent);
    if (!parentVisual)
        return 0;
    const VisualId id = m_next++;
    parentVisual->children.push_back(id);
    parentVisual->subtreeDirty = true;
    m_visuals.emplace(id, Visual{.parent = parent});
    invalidateAncestors(parent);
    m_dirty = true;
    return id;
}

bool Compositor::remove(VisualId id)
{
    Visual* visual = find(id);
    if (!visual || id == m_root)
        return false;
    Visual& parent = m_visuals.at(visual->parent);
    std::erase(parent.children, id);
    parent.subtreeDirty = true;
    invalidateAncestors(visual->parent);
    std::vector<VisualId> pending{id};
    while (!pending.empty())
    {
        const VisualId current = pending.back();
        pending.pop_back();
        const auto it = m_visuals.find(current);
        pending.insert(pending.end(), it->second.children.begin(), it->second.children.end());
        evict(current);
        m_visuals.erase(it);
    }
    m_dirty = true;
    return true;
}

bool Compositor::reorder(VisualId id, std::size_t index)
{
    Visual* visual = find(id);
    if (!visual || id == m_root)
        return false;
    auto& siblings = m_visuals.at(visual->parent).children;
    std::erase(siblings, id);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), id);
    m_visuals.at(visual->parent).subtreeDirty = true;
    invalidateAncestors(visual->parent);
    m_dirty = true;
    return true;
}

void Compositor::invalidateAncestors(VisualId id)
{
    for (Visual* visual = find(id); visual; visual = find(visual->parent))
        visual->subtreeDirty = true;
}

template <class F> bool Compositor::update(VisualId id, F&& apply)
{
    Visual* visual = find(id);
    if (!visual)
        return false;
    if (apply(*visual))
    {
        invalidateAncestors(visual->parent);
        m_dirty = true;
    }
    return true;
}

bool Compositor::setOffset(VisualId id, Point offset)
{
    return update(id,
                  [&](Visual& v)
                  {
                      if (v.offset.x == offset.x && v.offset.y == offset.y)
                          return false;
                      v.offset = offset;
                      return true;
                  });
}

bool Compositor::setTransform(VisualId id, Transform transform, Point anchor)
{
    return update(id,
                  [&](Visual& v)
                  {
                      v.transform = transform;
                      v.anchor = anchor;
                      return true;
                  });
}

bool Compositor::setOpacity(VisualId id, float opacity)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    return update(id,
                  [&](Visual& v)
                  {
                      if (v.opacity == opacity)
                          return false;
                      v.opacity = opacity;
                      return true;
                  });
}

bool Compositor::setClip(VisualId id, VisualClip clip)
{
    return update(id,
                  [&](Visual& v)
                  {
                      v.clip = clip;
                      return true;
                  });
}

bool Compositor::setZIndex(VisualId id, int zIndex)
{
    return update(id,
                  [&](Visual& v)
                  {
                      if (v.zIndex == zIndex)
                          return false;
                      v.zIndex = zIndex;
                      return true;
                  });
}

bool Compositor::setVisible(VisualId id, bool visible)
{
    return update(id,
                  [&](Visual& v)
                  {
                      if (v.visible == visible)
                          return false;
                      v.visible = visible;
                      return true;
                  });
}

bool Compositor::setEffects(VisualId id, std::vector<Effect> effects)
{
    return update(id,
                  [&](Visual& v)
                  {
                      v.effects = std::move(effects);
                      return true;
                  });
}

bool Compositor::setContent(VisualId id, DisplayList content)
{
    return update(id,
                  [&](Visual& v)
                  {
                      Bounds bounds;
                      for (const auto& command : content.commands())
                      {
                          if (std::holds_alternative<ClearCommand>(command) ||
                              std::holds_alternative<TransformCommand>(command))
                              bounds.unbounded = true;
                          else if (const auto* r = std::get_if<RectCommand>(&command))
                              bounds.add({false, inflate(r->bounds, r->paint)});
                          else if (const auto* r = std::get_if<RoundedRectCommand>(&command))
                              bounds.add({false, inflate(r->bounds, r->paint)});
                          else if (const auto* t = std::get_if<TextCommand>(&command))
                              bounds.add({false, t->bounds});
                          else if (const auto* i = std::get_if<ImageCommand>(&command))
                              bounds.add({false, i->bounds});
                          else if (const auto* p = std::get_if<PathCommand>(&command))
                          {
                              for (const auto& e : p->elements)
                              {
                                  const Point points[] = {e.p1, e.p2, e.p3};
                                  const int used = e.verb == PathVerb::kClose     ? 0
                                                   : e.verb == PathVerb::kQuadTo  ? 2
                                                   : e.verb == PathVerb::kCubicTo ? 3
                                                                                  : 1;
                                  for (int i = 0; i < used; ++i)
                                      bounds.add(
                                          {false, inflate({points[i].x, points[i].y, 0, 0}, p->paint)});
                              }
                          }
                      }
                      v.content = std::make_shared<const DisplayList>(std::move(content));
                      v.contentBounds = bounds;
                      v.contentDirty = true;
                      return true;
                  });
}

bool Compositor::setLayer(VisualId id, bool layer)
{
    Visual* visual = find(id);
    if (!visual)
        return false;
    visual->layer = layer;
    if (!layer)
        evict(id);
    return true;
}

void Compositor::setBackground(Color color)
{
    m_background = color;
    m_dirty = true;
}

void Compositor::resize(float width, float height, float dpi)
{
    if (width == m_width && height == m_height && dpi == m_dpi)
        return;
    m_width = width;
    m_height = height;
    m_dpi = dpi > 0 ? dpi : 1;
    m_dirty = true;
}

SceneSnapshot Compositor::snapshot()
{
    FrameStats stats;
    std::vector<Command> out;
    out.push_back(ClearCommand{m_background});
    lower(m_root, out, stats); // in DIPs; the presenter maps them to device pixels with m_dpi
    stats.commands = out.size();
    m_dirty = false;
    return {++m_frame, m_width, m_height, m_dpi, std::make_shared<const DisplayList>(std::move(out)), stats};
}

Compositor::Bounds Compositor::lower(VisualId id, std::vector<Command>& out, FrameStats& stats)
{
    Visual& v = m_visuals.at(id);
    if (!v.visible || v.opacity <= 0)
        return {}; // dirty flags stay set until the visual is drawn again
    ++stats.visualsLowered;

    const auto& t = v.transform;
    const bool hasAnchor = v.anchor.x != 0 || v.anchor.y != 0;
    const bool hasTransform = v.offset.x != 0 || v.offset.y != 0 || t.translateX != 0 || t.translateY != 0 ||
                              t.scaleX != 1 || t.scaleY != 1 || t.rotationDegrees != 0;
    const Affine matrix =
        translate(v.offset.x + t.translateX + v.anchor.x, v.offset.y + t.translateY + v.anchor.y) *
        scale(t.scaleX, t.scaleY) * rotate(t.rotationDegrees) * translate(-v.anchor.x, -v.anchor.y);
    std::size_t restores = 0;
    if (hasTransform)
    {
        out.push_back(
            TransformCommand{{v.offset.x + t.translateX + v.anchor.x, v.offset.y + t.translateY + v.anchor.y,
                              t.scaleX, t.scaleY, t.rotationDegrees}});
        ++restores;
        if (hasAnchor)
        {
            out.push_back(TransformCommand{{-v.anchor.x, -v.anchor.y, 1, 1, 0}});
            ++restores;
        }
    }
    const bool clipped = v.clip.kind != VisualClip::Kind::kNone;
    if (clipped)
    {
        if (!hasTransform)
        {
            out.push_back(SaveCommand{});
            ++restores;
        }
        if (v.clip.kind == VisualClip::Kind::kRect)
            out.push_back(ClipRectCommand{v.clip.bounds});
        else
            out.push_back(ClipRoundedRectCommand{v.clip.bounds, v.clip.radius});
    }
    const auto applyClip = [&](Bounds b)
    {
        if (!clipped)
            return b;
        if (b.unbounded)
            b.rect = v.clip.bounds;
        else if (b.rect)
            b.rect = intersect(*b.rect, v.clip.bounds);
        b.unbounded = false;
        return b;
    };

    float blur = 0;
    bool shadows = false;
    for (const auto& e : v.effects)
    {
        if (e.kind == Effect::Kind::kBlur)
            blur = std::max(blur, e.radius);
        else
            shadows = true;
    }
    const bool group = v.opacity < 1 || blur > 0;
    const auto groupBounds = [&](const Bounds& b)
    { return b.unbounded ? kUnboundedRect : inflate(b.rect.value_or(Rect{}), 3 * blur); };

    Bounds inner, extent;
    if (!v.layer && !shadows)
    {
        // Direct path: the group bounds are patched once the content extent is known.
        const std::size_t layerIndex = out.size();
        if (group)
            out.push_back(SaveLayerCommand{{}, v.opacity, blur});
        inner = applyClip(lowerInner(v, out, stats));
        if (group)
        {
            std::get<SaveLayerCommand>(out[layerIndex]).bounds = groupBounds(inner);
            out.push_back(RestoreCommand{});
        }
        extent = inner;
    }
    else
    {
        // The body is emitted more than once (shadows) or retained (layer), so lower it separately.
        std::shared_ptr<const CommandList> body;
        u64 version = 0;
        const Fragment* hit = v.layer && !v.contentDirty && !v.subtreeDirty ? cached(id) : nullptr;
        if (hit)
        {
            ++stats.layerHits;
            body = hit->commands;
            inner = hit->bounds;
            version = hit->version;
        }
        else
        {
            auto list = std::make_shared<CommandList>();
            inner = lowerInner(v, list->commands, stats);
            body = std::move(list);
            if (v.layer)
            {
                ++stats.layerMisses;
                version = ++m_fragmentVersion;
                store(id, Fragment{body, inner, version});
            }
        }
        // A bounded layer becomes a LayerRef the backend can rasterize once and reuse.
        const bool retained =
            v.layer && !inner.unbounded && inner.rect && inner.rect->width > 0 && inner.rect->height > 0;
        const auto emitBody = [&]
        {
            if (retained)
                out.push_back(LayerRef{id, version, *inner.rect, body});
            else
                out.insert(out.end(), body->commands.begin(), body->commands.end());
        };
        const Bounds visible = applyClip(inner);
        extent = visible;
        for (const auto& e : v.effects)
        {
            if (e.kind != Effect::Kind::kDropShadow)
                continue;
            out.push_back(TransformCommand{{e.offset.x, e.offset.y, 1, 1, 0}});
            const Rect shadow =
                visible.unbounded ? kUnboundedRect : inflate(visible.rect.value_or(Rect{}), 3 * e.radius);
            out.push_back(
                SaveLayerCommand{shadow, e.color.a, e.radius, Color{e.color.r, e.color.g, e.color.b, 1}});
            emitBody();
            out.push_back(RestoreCommand{});
            out.push_back(RestoreCommand{});
            ++stats.effectsApplied;
            if (!visible.unbounded && visible.rect)
                extent.add(
                    {false, Rect{shadow.x + e.offset.x, shadow.y + e.offset.y, shadow.width, shadow.height}});
        }
        if (group)
            out.push_back(SaveLayerCommand{groupBounds(visible), v.opacity, blur});
        emitBody();
        if (group)
            out.push_back(RestoreCommand{});
        extent = applyClip(extent);
    }
    if (blur > 0)
    {
        ++stats.effectsApplied;
        if (extent.rect && !extent.unbounded)
            extent.rect = inflate(*extent.rect, 3 * blur);
        extent = applyClip(extent);
    }
    out.insert(out.end(), restores, RestoreCommand{});
    v.contentDirty = v.subtreeDirty = false;

    Bounds result;
    result.unbounded = extent.unbounded;
    if (extent.rect)
        result.rect = transformRect(matrix, *extent.rect);
    return result;
}

Compositor::Bounds Compositor::lowerInner(Visual& visual, std::vector<Command>& out, FrameStats& stats)
{
    Bounds bounds;
    if (visual.content)
    {
        const auto& commands = visual.content->commands();
        out.insert(out.end(), commands.begin(), commands.end());
        bounds.add(visual.contentBounds);
    }
    const bool ordered =
        std::ranges::all_of(visual.children, [&](VisualId c) { return m_visuals.at(c).zIndex == 0; });
    if (ordered)
    {
        for (const VisualId child : visual.children)
            bounds.add(lower(child, out, stats));
        return bounds;
    }
    std::vector<VisualId> sorted = visual.children;
    std::ranges::stable_sort(sorted, {}, [&](VisualId c) { return m_visuals.at(c).zIndex; });
    for (const VisualId child : sorted)
        bounds.add(lower(child, out, stats));
    return bounds;
}

const Compositor::Fragment* Compositor::cached(VisualId id)
{
    const auto it = m_cache.find(id);
    if (it == m_cache.end())
        return nullptr;
    m_cacheLru.splice(m_cacheLru.begin(), m_cacheLru, it->second.lru);
    return &it->second.fragment;
}

void Compositor::store(VisualId id, Fragment fragment)
{
    evict(id);
    fragment.bytes = estimateBytes(fragment.commands->commands);
    if (fragment.bytes > m_cacheBudget)
        return;
    while (m_cacheBytes + fragment.bytes > m_cacheBudget)
    {
        evict(m_cacheLru.back());
        ++m_cacheEvictions;
    }
    m_cacheBytes += fragment.bytes;
    m_cacheLru.push_front(id);
    m_cache.emplace(id, CacheEntry{std::move(fragment), m_cacheLru.begin()});
}

void Compositor::evict(VisualId id)
{
    const auto it = m_cache.find(id);
    if (it == m_cache.end())
        return;
    m_cacheBytes -= it->second.fragment.bytes;
    m_cacheLru.erase(it->second.lru);
    m_cache.erase(it);
}

LayerCacheStats Compositor::layerCache() const noexcept
{
    return {m_cache.size(), m_cacheBytes, m_cacheBudget, m_cacheEvictions};
}

} // namespace velora
