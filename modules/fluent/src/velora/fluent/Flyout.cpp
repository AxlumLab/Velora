#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/Flyout.hpp>

namespace velora
{
Flyout::Flyout(ContentFactory factory) : m_factory(std::move(factory))
{
    if (!m_factory)
        throw std::invalid_argument("Flyout requires a content factory");
}
Flyout::~Flyout()
{
    hide();
}
bool Flyout::isOpen() const noexcept
{
    return m_ui && !m_lifetime.expired() && m_root && m_ui->popupRoot() == m_root;
}
ElementId Flyout::anchor() const noexcept
{
    return isOpen() ? m_anchor : 0;
}
bool Flyout::isOpenAt(const Framework& ui, ElementId anchor) const noexcept
{
    return m_ui == &ui && m_anchor == anchor && isOpen();
}
const std::string& Flyout::lastError() const noexcept
{
    return m_error;
}
void Flyout::hide()
{
    const auto root = m_root;
    if (isOpen())
        m_ui->closePopup();
    if (m_root == root)
    {
        m_ui = nullptr;
        m_root = m_anchor = 0;
        m_lifetime.reset();
    }
}
bool Flyout::showAt(Framework& ui, ElementId anchor, std::function<void(bool)> changed)
{
    if (isOpen() && m_ui == &ui && m_anchor == anchor)
        return true;
    if (m_opening || !ui.isOwnerThread() || weak_from_this().expired())
        return false;
    const auto keepAlive = shared_from_this();
    const auto lifetime = ui.lifetime();
    m_opening = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_opening};
    hide();
    m_error.clear();
    ElementId root = 0;
    try
    {
        if (lifetime.expired() || !ui.tree().contains(anchor))
            throw std::runtime_error("Flyout anchor is not attached");
        const auto loaded =
            loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/FlyoutView.vxbf"));
        if (!loaded.ok)
            throw std::runtime_error("Flyout presenter: " + loaded.error);
        root = ui.tree().element(*loaded.root);
        ui.set(root, elementProperties().visual.clipToBounds, true);
        const auto content = m_factory(ui);
        if (lifetime.expired())
            throw std::runtime_error("Flyout UI context was destroyed by content factory");
        if (!content || content == anchor || !ui.tree().contains(content) || ui.tree().parent(content) ||
            ui.isWithin(anchor, content) || !ui.tree().adopt(root, content, 0))
            throw std::runtime_error("Flyout factory must return a fresh independent root");
        m_ui = &ui;
        m_lifetime = lifetime;
        m_root = root;
        m_anchor = anchor;
        const auto weak = weak_from_this();
        const bool opened =
            ui.openPopup(anchor, root,
                         [weak, root, changed]
                         {
                             if (const auto owner = weak.lock(); owner && owner->m_root == root)
                             {
                                 owner->m_ui = nullptr;
                                 owner->m_root = owner->m_anchor = 0;
                                 owner->m_lifetime.reset();
                             }
                             if (changed)
                                 changed(false);
                         });
        if (!opened || m_root != root)
            throw std::runtime_error("Flyout anchor is unavailable or opening was cancelled");
        if (changed)
            changed(true);
        return isOpen();
    }
    catch (const std::exception& error)
    {
        m_error = error.what();
    }
    catch (...)
    {
        m_error = "Flyout content factory failed";
    }
    if (!lifetime.expired())
    {
        if (ui.popupRoot() == root)
            ui.closePopup(false);
        else if (root && ui.tree().contains(root))
            ui.tree().remove(root);
    }
    if (m_root == root)
    {
        m_ui = nullptr;
        m_root = m_anchor = 0;
        m_lifetime.reset();
    }
    return false;
}
} // namespace velora
