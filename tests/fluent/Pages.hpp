#pragma once

// Controllers of the Fluent package test pages.
namespace FluentTest
{
struct Plain
{
};
struct Overridden
{
};
struct FirstBatch
{
    int textChanged = 0, checkedCount = 0, clicks = 0, viewChanged = 0;
    void OnTextChanged()
    {
        ++textChanged;
    }
    void OnChecked()
    {
        ++checkedCount;
    }
    void OnClick()
    {
        ++clicks;
    }
    void OnViewChanged()
    {
        ++viewChanged;
    }
};
struct SecondBatch
{
    int toggled = 0, valueChanged = 0, large = 0;
    void OnToggled()
    {
        ++toggled;
    }
    void OnValueChanged()
    {
        ++valueChanged;
    }
    void OnLarge()
    {
        ++large;
    }
};
} // namespace FluentTest
