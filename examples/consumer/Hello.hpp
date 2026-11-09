#pragma once

#include <string>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Markers.hpp>

namespace Consumer
{

rkclass Hello : public velora::Observable
{
public:
    rkproperty(Name, std::string, get = name, set = setName)

        const std::string&
        name() const
    {
        return m_name;
    }
    void setName(std::string value)
    {
        set(m_name, std::move(value), "Name");
    }

    cfunc void OnGreet()
    {
        ++m_greetings;
    }
    int greetings() const
    {
        return m_greetings;
    }

private:
    std::string m_name = "Velora";
    int m_greetings = 0;
};

} // namespace Consumer
