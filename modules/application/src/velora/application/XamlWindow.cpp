#include "velora/application/XamlWindow.hpp"

#include <velora/application/XamlRoot.hpp>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlHost.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/text/TextService.hpp>

namespace velora
{

XamlWindow::XamlWindow(XamlApplication& app) : m_app(app), m_host(std::make_unique<XamlHost>())
{
    m_app.registerWindow(this);
}

XamlWindow::~XamlWindow()
{
    Close();
    m_host->detach();
    m_app.unregisterWindow(this);
}

XamlRoot& XamlWindow::GetXamlRoot()
{
    if (!m_root)
    {
        // Text measurement is the application's shared TextService; everything else is per root.
        TextService* text = m_app.hasTextService() ? &m_app.textService() : nullptr;
        m_root = text ? std::make_unique<XamlRoot>([text](std::string_view s, float size)
                                                   { return text->measureWidth(s, size); })
                      : std::make_unique<XamlRoot>();
    }
    return *m_root;
}

Framework& XamlWindow::GetFramework()
{
    return GetXamlRoot().framework();
}

bool XamlWindow::Create(const WindowOptions& options)
{
    // Request a window from the Application.
    m_window = m_app.application().createWindow(options);
    if (!m_window)
        return false;

    // Attach the XamlHost to the window with this window's own root.
    XamlRoot& root = GetXamlRoot();
    if (!m_host->attach(*m_window, root.framework(), root.compositor(), m_app.graphics()))
        return false;
    // The root follows its own window only (R04.6): size, DPI and visibility.
    m_host->setEventHandler([this](AppEvent&) { syncHostState(); });
    syncHostState();
    return true;
}

void XamlWindow::syncHostState()
{
    if (!m_root || !m_window)
        return;
    const bool visible = !m_window->isClosed() && m_window->isVisible() && !m_window->isMinimized();
    m_root->setHostState(m_window->getWidth(), m_window->getHeight(), m_window->getDpiScale(), visible);
}

void XamlWindow::Show()
{
    if (m_window && !m_window->isClosed())
    {
        m_window->show();
        syncHostState();
    }
}

void XamlWindow::Close()
{
    if (m_window && !m_window->isClosed())
        m_window->close();
}

bool XamlWindow::SetRoot(u64 element)
{
    return GetXamlRoot().setContent(element);
}

bool XamlWindow::IsClosed() const
{
    return !m_window || m_window->isClosed();
}

void XamlWindow::Update()
{
    if (m_host && m_host->isAttached())
        m_host->update();
}

bool XamlWindow::HasPendingWork() const
{
    return m_host && m_host->hasPendingWork();
}

} // namespace velora
