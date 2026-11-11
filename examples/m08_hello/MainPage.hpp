#pragma once

#include <string>
#include <string_view>
#include <velora/reflect/Markers.hpp>

namespace Demo
{

// View model: plain C++ with reflected accessors. Bindings call these directly.
rkclass MainViewModel
{
public:
    rkproperty(UserName, std::string, get = userName, set = setUserName)
    rkproperty(Greeting, std::string, get = greeting)
    rkproperty(Status, std::string, get = status)
    rkproperty(SaveCount, int, get = saveCount)

    const std::string& userName() const
    {
        return m_userName;
    }
    void setUserName(std::string value)
    {
        m_userName = std::move(value);
    }
    std::string greeting() const
    {
        return m_userName.empty() ? "Hello, XAML" : "Hello, " + m_userName;
    }
    std::string status() const
    {
        return m_saveCount == 0
                   ? "Not saved"
                   : "Saved " + std::to_string(m_saveCount) + (m_saveCount == 1 ? " time" : " times");
    }
    int saveCount() const
    {
        return m_saveCount;
    }
    void save()
    {
        ++m_saveCount;
    }

private:
    std::string m_userName;
    int m_saveCount = 0;
};

// Controller named by x:Class. The generated MainPage_xaml view holds a reference to it.
rkclass MainPage
{
public:
    rkproperty(ViewModel, MainViewModel&, get = viewModel)
    rkproperty(Title, std::string_view, get = title)

    const MainViewModel& viewModel() const
    {
        return m_viewModel;
    }
    MainViewModel& viewModel()
    {
        return m_viewModel;
    }
    std::string_view title() const
    {
        return "Velora XAML";
    }

    cfunc void OnSave()
    {
        m_viewModel.save();
    }

private:
    MainViewModel m_viewModel;
};

} // namespace Demo
