#include <algorithm>
#include <velora/application/XamlRoot.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/runtime/property/DependencyObject.hpp>

namespace velora
{

namespace
{
// Live roots of the calling (UI) thread, for XamlRoot::of. Roots are created and destroyed on
// their UI thread, so no lock is needed.
thread_local std::vector<XamlRoot*> t_roots;
} // namespace

XamlRoot::XamlRoot(TextMeasurer measure)
    : m_compositor(std::make_unique<Compositor>()),
      m_framework(std::make_unique<Framework>(*m_compositor, std::move(measure)))
{
    t_roots.push_back(this);
}

XamlRoot::~XamlRoot()
{
    std::erase(t_roots, this);
    // The framework references the compositor; destroy it first.
    m_framework.reset();
    m_compositor.reset();
}

bool XamlRoot::setContent(ElementId element)
{
    Framework& ui = *m_framework;
    if (!ui.isOwnerThread())
        return false;
    if (element && (!ui.tree().contains(element) || ui.tree().parent(element)))
        return false;
    if (element == m_content)
        return ui.setMounted(element, true) || !element;
    if (m_content && ui.tree().contains(m_content))
        ui.setMounted(m_content, false);
    if (element && !ui.setMounted(element, true))
        return false;
    m_content = element;
    raise(kContentChanged);
    return true;
}

bool XamlRoot::setContent(const DependencyObject& element)
{
    // Identity, not ElementId: ids are per tree and may coincide between roots.
    const ElementId id = m_framework->tree().element(element);
    if (!id)
        return false; // not an element of this root (another root's, or no tree at all)
    return setContent(id);
}

XamlRoot* XamlRoot::of(const DependencyObject& object)
{
    for (XamlRoot* root : t_roots)
        if (const ElementId id = root->m_framework->tree().element(object); id && root->contains(id))
            return root;
    return nullptr;
}

bool XamlRoot::contains(ElementId element) const
{
    return m_content && element && m_framework->isMounted(m_content) &&
           m_framework->isWithin(element, m_content);
}

void XamlRoot::setHostState(f32 width, f32 height, f32 scale, bool visible)
{
    u32 changes = 0;
    if (width != m_width || height != m_height)
        changes |= kSizeChanged;
    if (scale != m_context.scale)
        changes |= kRasterizationScaleChanged;
    if (visible != m_hostVisible)
        changes |= kHostVisibilityChanged;
    m_width = width;
    m_height = height;
    m_context.scale = scale;
    m_hostVisible = visible;
    if (changes)
        raise(changes);
}

void XamlRoot::setTheme(Theme theme)
{
    if (theme == m_framework->theme())
        return;
    m_framework->setTheme(theme);
    m_context.theme = theme == Theme::kDark           ? ResourceTheme::kDark
                      : theme == Theme::kHighContrast ? ResourceTheme::kHighContrast
                                                      : ResourceTheme::kLight;
    raise(kThemeChanged);
}

void XamlRoot::setLanguage(std::string language)
{
    if (language == m_context.language)
        return;
    m_context.language = std::move(language);
    raise(kLanguageChanged);
}

u64 XamlRoot::addChanged(ChangedHandler handler)
{
    if (!handler)
        return 0;
    const u64 token = m_nextToken++;
    m_handlers.emplace_back(token, std::make_shared<ChangedHandler>(std::move(handler)));
    return token;
}

bool XamlRoot::removeChanged(u64 token)
{
    return std::erase_if(m_handlers, [&](const auto& entry) { return entry.first == token; }) != 0;
}

void XamlRoot::raise(u32 changes)
{
    // A snapshot, so handlers may add or remove handlers while being called.
    const auto handlers = m_handlers;
    for (const auto& [token, handler] : handlers)
        (*handler)(*this, changes);
}

} // namespace velora
