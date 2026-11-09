#pragma once

#include <string>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Markers.hpp>

// Reflected controller and view model of XBound.xaml: x:Bind paths are checked against this
// metadata at build time and compiled into accessors.
namespace VxbfTest
{

rkclass Settings : public velora::Observable
{
public:
    rkproperty(Title, std::string, get = title, set = setTitle)
        rkproperty(Enabled, bool, get = enabled, set = setEnabled)
            rkproperty(Size, double, get = size, set = setSize)

                const std::string&
                title() const
    {
        return m_title;
    }
    void setTitle(std::string value)
    {
        set(m_title, std::move(value), "Title");
    }
    bool enabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool value)
    {
        set(m_enabled, value, "Enabled");
    }
    double size() const
    {
        return m_size;
    }
    void setSize(double value)
    {
        set(m_size, value, "Size");
    }

private:
    std::string m_title = "first";
    bool m_enabled = false;
    double m_size = 12;
};

rkclass Person : public velora::Observable
{
public:
    rkproperty(Name, std::string, get = name, set = setName)

        Person() = default;
    explicit Person(std::string name) : m_name(std::move(name)) {}
    const std::string& name() const
    {
        return m_name;
    }
    void setName(std::string value)
    {
        set(m_name, std::move(value), "Name");
    }

private:
    std::string m_name;
};

// Controller of Listed.xaml: a ListView over People through a DataTemplate.
rkclass Listed
{
public:
    rkproperty(People, velora::ObservableVector<Person>&, get = people)

        velora::ObservableVector<Person>&
        people()
    {
        return m_people;
    }
    const velora::ObservableVector<Person>& people() const
    {
        return m_people;
    }

private:
    velora::ObservableVector<Person> m_people;
};

rkclass XBound
{
public:
    rkproperty(Model, Settings&, get = model)

        Settings&
        model()
    {
        return m_model;
    }
    const Settings& model() const
    {
        return m_model;
    }

private:
    Settings m_model;
};

} // namespace VxbfTest
