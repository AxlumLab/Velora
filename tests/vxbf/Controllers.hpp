#pragma once

#include <vector>
#include <velora/runtime/Framework.hpp>

// Code-behind controllers of the .vxbf test pages.
namespace VxbfTest
{

struct Counter
{
    int clicks = 0;
    void OnClick()
    {
        ++clicks;
    }
};

namespace A
{
struct Main : Counter
{
};
} // namespace A

namespace B
{
struct Main : Counter
{
};
} // namespace B

struct Styled : Counter
{
};

struct Broken : Counter
{
};

struct Themed : Counter
{
    // OnApplyTemplate: each templated element of the page, and its Chrome part at that moment.
    std::vector<velora::ElementId> templated, chromes;
    void OnApplyTemplate(velora::Framework& ui, velora::ElementId owner)
    {
        templated.push_back(owner);
        chromes.push_back(ui.templateChild(owner, "Chrome"));
    }
};

struct Cycle : Counter
{
};

struct Bound : Counter
{
};

struct Localized : Counter
{
};

struct States : Counter
{
};

} // namespace VxbfTest
