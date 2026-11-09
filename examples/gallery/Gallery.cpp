#include "Gallery.hpp"

#include <cmath>

namespace Gallery
{

GalleryPage::GalleryPage()
{
    for (const char* name : {"Apple", "Banana", "Cherry", "Date", "Elderberry", "Fig", "Grape", "Honeydew"})
        m_fruits.emplace_back(name);
}

void GalleryPage::OnGreet()
{
    set(m_greeting, m_name.empty() ? std::string("Hello, World!") : "Hello, " + m_name + "!", "Greeting");
}

void GalleryPage::OnSmall()
{
    setSize("Small");
}

void GalleryPage::OnMedium()
{
    setSize("Medium");
}

void GalleryPage::OnLarge()
{
    setSize("Large");
}

void GalleryPage::OnViewChanged()
{
    ++viewChanges;
}

void GalleryPage::setSubscribed(bool value)
{
    if (set(m_subscribed, value, "Subscribed"))
        raise("Summary");
}

void GalleryPage::setNotifications(bool value)
{
    if (set(m_notifications, value, "Notifications"))
        raise("Summary");
}

void GalleryPage::setVolume(double value)
{
    if (set(m_volume, value, "Volume"))
        raise("Summary");
}

void GalleryPage::setSize(std::string value)
{
    if (set(m_size, std::move(value), "Size"))
        raise("Summary");
}

std::string GalleryPage::summary() const
{
    return m_size + ", " + (m_subscribed ? "subscribed" : "unsubscribed") + ", alerts " +
           (m_notifications ? "on" : "off") + ", " + std::to_string(std::lround(m_volume)) + "%";
}

} // namespace Gallery
