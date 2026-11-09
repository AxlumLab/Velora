#include "MainWindow.hpp"

namespace N04Example
{

void MainWindow::OnGreetClick()
{
    const std::string name = getName();
    if (!name.empty())
        setGreeting("Hello, " + name + "!");
    else
        setGreeting("Hello, World!");
}

} // namespace N04Example
