#pragma once

#include <string>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Markers.hpp>

namespace N04Example
{

rkclass MainWindow : public velora::Observable
{
public:
    MainWindow() = default;

    cfunc void OnGreetClick();

    rkproperty(Name, std::string, get = getName, set = setName)
    rkproperty(Greeting, std::string, get = getGreeting, set = setGreeting)

    const std::string& getName() const { return m_name; }
    void setName(std::string value) { set(m_name, std::move(value), "Name"); }

    const std::string& getGreeting() const { return m_greeting; }
    void setGreeting(std::string value) { set(m_greeting, std::move(value), "Greeting"); }

private:
    std::string m_name;
    std::string m_greeting;
};

} // namespace N04Example
