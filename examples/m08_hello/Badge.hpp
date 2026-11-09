#pragma once

#include <string>
#include <velora/reflect/Markers.hpp>

namespace Demo
{

// Controller of the Badge custom control (Badge.xaml).
rkclass Badge
{
public:
    rkproperty(Label, std::string, get = label, set = setLabel)

        const std::string&
        label() const
    {
        return m_label;
    }
    void setLabel(std::string value)
    {
        m_label = std::move(value);
    }

private:
    std::string m_label;
};

} // namespace Demo
