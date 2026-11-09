#pragma once

#include <functional>
#include <vector>
#include <velora/reflect/Markers.hpp>
#include <velora/runtime/Framework.hpp>

namespace M10
{

// Controller of StylesPage.xaml.
rkclass StylesPage
{
public:
    cfunc void OnDefault()
    {
        ++defaultClicks;
    }
    cfunc void OnPillA()
    {
        ++pillAClicks;
    }
    cfunc void OnPillB()
    {
        ++pillBClicks;
    }
    cfunc void OnToggleTheme()
    {
        if (toggleTheme)
            toggleTheme();
    }
    // Called for every templated element once its template instance exists (OnApplyTemplate).
    cfunc void OnApplyTemplate(velora::Framework & ui, velora::ElementId owner)
    {
        applied.push_back(
            {owner, ui.templateChild(owner, "Chrome"), ui.templateChild(owner, "ContentPresenter")});
    }

    struct Applied
    {
        velora::ElementId owner, chrome, presenter;
    };
    std::vector<Applied> applied;
    int defaultClicks = 0, pillAClicks = 0, pillBClicks = 0;
    std::function<void()> toggleTheme;
};

} // namespace M10
