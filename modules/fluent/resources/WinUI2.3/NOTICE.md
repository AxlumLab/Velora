# WinUI 2.3 excerpts

`WinUIButton.xaml`, `WinUITextBox.xaml` and `WinUICornerRadius.xaml` are excerpts of
`dev/CommonStyles/Button_themeresources.xaml`, `TextBox_themeresources.xaml` and
`CornerRadius_themeresources.xaml` from microsoft/microsoft-ui-xaml tag `v2.3.191129002`
(commit `2237b09d9ba6052414ddccf3d61316de20564303`). Each file's header comment lists what was
removed and why; the remaining entries are unchanged. `SystemResources.xaml` is not from WinUI: it
stands in for the platform resources those dictionaries refer to.

They are licensed under the MIT License:

    Copyright (c) Microsoft Corporation. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy of this
    software and associated documentation files (the "Software"), to deal in the Software
    without restriction, including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
    to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or
    substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
    PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
    FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

Parsing them does not mean Velora implements WinUI XAML; it shows the front end accepts these
real dictionaries (conditional namespaces, theme dictionaries, templates, visual states,
TemplateBinding and Binding with RelativeSource).
