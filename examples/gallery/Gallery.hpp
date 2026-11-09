#pragma once

#include <string>
#include <utility>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Markers.hpp>

// Controller and models of the Velora control gallery (GalleryPage.xaml).
namespace Gallery
{

rkclass Fruit : public velora::Observable
{
public:
    rkproperty(Name, std::string, get = name)

    Fruit() = default;
    explicit Fruit(std::string name) : m_name(std::move(name)) {}
    const std::string& name() const
    {
        return m_name;
    }

private:
    std::string m_name;
};

// DataContext of the newsletter CheckBox, bound with {Binding Newsletter, Mode=TwoWay}.
rkclass Settings : public velora::Observable
{
public:
    rkproperty(Newsletter, bool, get = newsletter, set = setNewsletter)

    bool newsletter() const
    {
        return m_newsletter;
    }
    void setNewsletter(bool value)
    {
        set(m_newsletter, value, "Newsletter");
    }

private:
    bool m_newsletter = false;
};

rkclass GalleryPage : public velora::Observable
{
public:
    rkproperty(Name, std::string, get = name, set = setName)
    rkproperty(Greeting, std::string, get = greeting)
    rkproperty(Subscribed, bool, get = subscribed, set = setSubscribed)
    rkproperty(Notifications, bool, get = notifications, set = setNotifications)
    rkproperty(Volume, double, get = volume, set = setVolume)
    rkproperty(Size, std::string, get = size)
    rkproperty(Summary, std::string, get = summary)
    rkproperty(Fruits, velora::ObservableVector<Fruit>&, get = fruits)
    rkproperty(Settings, Gallery::Settings&, get = settings)

    GalleryPage();

    cfunc void OnGreet();
    cfunc void OnSmall();
    cfunc void OnMedium();
    cfunc void OnLarge();
    cfunc void OnViewChanged();

    const std::string& name() const
    {
        return m_name;
    }
    void setName(std::string value)
    {
        set(m_name, std::move(value), "Name");
    }
    const std::string& greeting() const
    {
        return m_greeting;
    }
    bool subscribed() const
    {
        return m_subscribed;
    }
    void setSubscribed(bool value);
    bool notifications() const
    {
        return m_notifications;
    }
    void setNotifications(bool value);
    double volume() const
    {
        return m_volume;
    }
    void setVolume(double value);
    const std::string& size() const
    {
        return m_size;
    }
    std::string summary() const;
    velora::ObservableVector<Fruit>& fruits()
    {
        return m_fruits;
    }
    const velora::ObservableVector<Fruit>& fruits() const
    {
        return m_fruits;
    }
    Gallery::Settings& settings()
    {
        return m_settings;
    }
    const Gallery::Settings& settings() const
    {
        return m_settings;
    }

    int viewChanges = 0; // ViewChanged events of the page's ScrollViewer

private:
    void setSize(std::string value);

    std::string m_name, m_greeting, m_size = "Small";
    bool m_subscribed = false, m_notifications = true;
    double m_volume = 40;
    velora::ObservableVector<Fruit> m_fruits;
    Gallery::Settings m_settings;
};

} // namespace Gallery
