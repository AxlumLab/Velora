// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// RGB/HSV sector and chroma conversions adapted from porting/Common/ColorConversion.cpp;
// coordinated color updates and channel visibility from ColorPicker.cpp/ColorSpectrum.cpp.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/InputContext.hpp>
#include <velora/fluent/ColorPicker.hpp>
#include <velora/graphics/Canvas.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kPicker = "Velora.Fluent.ColorPicker", kSpectrum = "Velora.Fluent.ColorSpectrum";
Color paintColor(u32 c)
{
    return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, (c >> 24) / 255.0f};
}
bool validColor(const PropertyValue& v)
{
    return v.isInt() && v.asInt() >= 0 && v.asInt() <= 0xFFFFFFFFLL;
}
} // namespace
HsvColor rgbToHsv(u32 color)
{
    const f64 r = ((color >> 16) & 255) / 255.0, g = ((color >> 8) & 255) / 255.0, b = (color & 255) / 255.0;
    const auto maximum = std::max({r, g, b}), minimum = std::min({r, g, b}), chroma = maximum - minimum;
    HsvColor result{0, maximum > 0 ? chroma / maximum : 0, maximum};
    if (chroma > 0)
    {
        result.hue = 60 * (maximum == r   ? (g - b) / chroma
                           : maximum == g ? 2 + (b - r) / chroma
                                          : 4 + (r - g) / chroma);
        if (result.hue < 0)
            result.hue += 360;
    }
    return result;
}
u32 hsvToArgb(HsvColor hsv, f64 alpha)
{
    if (!std::isfinite(hsv.hue) || !std::isfinite(hsv.saturation) || !std::isfinite(hsv.value) ||
        !std::isfinite(alpha))
        throw std::invalid_argument("HSV channels must be finite");
    const f64 h = std::fmod(std::fmod(hsv.hue, 360) + 360, 360) / 60;
    const f64 chroma = std::clamp(hsv.saturation, 0.0, 1.0) * std::clamp(hsv.value, 0.0, 1.0),
              minimum = std::clamp(hsv.value, 0.0, 1.0) - chroma,
              x = chroma * (1 - std::abs(std::fmod(h, 2) - 1));
    const std::array<std::array<f64, 3>, 6> sectors{
        {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x}, {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}}};
    const auto& rgb = sectors[static_cast<usize>(h)];
    const auto byte = [](f64 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return byte(alpha) << 24 | byte(rgb[0] + minimum) << 16 | byte(rgb[1] + minimum) << 8 |
           byte(rgb[2] + minimum);
}
ColorSpectrum::ColorSpectrum() : UIElement(ElementKind::kUserControl)
{
}
std::string ColorSpectrum::className() const
{
    return std::string(kSpectrum);
}
const XamlTypeDeclaration& ColorSpectrum::declaration()
{
    return fluentDeclaration(kSpectrum);
}
std::string ColorSpectrum::registerType(Framework& ui)
{
    return registerFluentControl(ui, kSpectrum, [] { return createElement<ColorSpectrum>(); });
}
const PropertyDescriptor& ColorSpectrum::colorProperty()
{
    return fluentProperty(kSpectrum, "Color");
}
u32 ColorSpectrum::color() const
{
    return static_cast<u32>(getValue(colorProperty()).asInt());
}
PropertySetResult ColorSpectrum::setColor(u32 value)
{
    return setValue(colorProperty(), static_cast<i64>(value));
}
bool ColorSpectrum::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    return p.id != colorProperty().id || validColor(v);
}
Size ColorSpectrum::measureOverride(LayoutContext&, ElementId, const MeasureInput&)
{
    return {240, 140};
}
bool ColorSpectrum::renderOverride(Canvas& canvas, Size size) const
{
    const auto hsv = rgbToHsv(color());
    constexpr i32 kColumns = 48, kRows = 24;
    for (i32 y = 0; y < kRows; ++y)
        for (i32 x = 0; x < kColumns; ++x)
            canvas.drawRect(
                {x * size.width / kColumns, y * size.height / kRows, size.width / kColumns + .5f,
                 size.height / kRows + .5f},
                Paint{paintColor(hsvToArgb(
                    {360.0 * x / (kColumns - 1), 1.0 - static_cast<f64>(y) / (kRows - 1), hsv.value}))});
    canvas.drawRoundedRect({static_cast<f32>(hsv.hue / 360 * size.width) - 5,
                            static_cast<f32>((1 - hsv.saturation) * size.height) - 5, 10, 10},
                           5, Paint{Color{1, 1, 1, 1}, 2, true});
    return true;
}
bool ColorSpectrum::isFocusable() const noexcept
{
    return true;
}
void ColorSpectrum::pick(InputContext& context, const PointerEventArgs& args)
{
    const auto size = context.bounds();
    if (size.width <= 0 || size.height <= 0)
        return;
    const auto current = rgbToHsv(color());
    setColor(hsvToArgb({std::clamp<f64>(args.x / size.width, 0, 1) * 359,
                        1 - std::clamp<f64>(args.y / size.height, 0, 1), current.value},
                       (color() >> 24) / 255.0));
}
void ColorSpectrum::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    context.focus();
    if (context.capturePointer())
    {
        m_dragging = true;
        pick(context, args);
        args.handled = true;
    }
}
void ColorSpectrum::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (m_dragging && context.hasPointerCapture())
    {
        pick(context, args);
        args.handled = true;
    }
    else
        m_dragging = false;
}
void ColorSpectrum::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    if (context.hasPointerCapture())
    {
        context.releasePointer();
        pick(context, args);
    }
    args.handled = true;
}
void ColorSpectrum::onDetached() noexcept
{
    m_dragging = false;
}
void ColorSpectrum::onKeyDown(InputContext&, KeyEventArgs& args)
{
    auto hsv = rgbToHsv(color());
    if (args.key == Key::kLeft)
        hsv.hue -= 1;
    else if (args.key == Key::kRight)
        hsv.hue += 1;
    else if (args.key == Key::kUp)
        hsv.saturation += .01;
    else if (args.key == Key::kDown)
        hsv.saturation -= .01;
    else
        return;
    args.handled = true;
    setColor(hsvToArgb(hsv, (color() >> 24) / 255.0));
}
ColorPicker::ColorPicker() : FluentElement(kPicker, "ColorPicker")
{
}
const XamlTypeDeclaration& ColorPicker::declaration()
{
    return fluentDeclaration(kPicker);
}
std::string ColorPicker::registerType(Framework& ui)
{
    return registerFluentControl(ui, kPicker, [] { return createElement<ColorPicker>(); });
}
const PropertyDescriptor& ColorPicker::colorProperty()
{
    return fluentProperty(kPicker, "Color");
}
u32 ColorPicker::color() const
{
    return static_cast<u32>(getValue(colorProperty()).asInt());
}
PropertySetResult ColorPicker::setColor(u32 value)
{
    return setValue(colorProperty(), static_cast<i64>(value));
}
PropertySetResult ColorPicker::setHsv(HsvColor hsv, f64 alpha)
{
    return setColor(hsvToArgb(hsv, alpha));
}
bool ColorPicker::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    return p.id == colorProperty().id ? validColor(v) : FluentElement::validatePropertyValue(p, v);
}
std::string ColorPicker::hex() const
{
    std::array<char, 12> text{};
    std::snprintf(text.data(), text.size(), "#%08X", color());
    return text.data();
}
bool ColorPicker::setHex(std::string_view text)
{
    if (!isOwnerThread())
        throw std::logic_error("ColorPicker belongs to its UI thread");
    if (text.starts_with('#'))
        text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return false;
    u32 value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return false;
    if (text.size() == 6)
        value |= 0xFF000000;
    return setColor(value) == PropertySetResult::kOk;
}
void ColorPicker::commitHex()
{
    if (!m_ui || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* ui = m_ui;
    const auto text = ui->tree().property(part("HexTextBox"), *ui->props().Text).asString();
    const bool valid = setHex(text);
    if (m_ui == ui)
        ui->set(part("ErrorText"), ui->props().Text, valid ? "" : "Use #RRGGBB or #AARRGGBB");
}
void ColorPicker::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_spectrum = createElement<ColorSpectrum>();
    ui.insert(m_spectrum, part("SpectrumHost"));
    const auto token = m_spectrum->addPropertyChanged(
        [weak](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&,
               const PropertyValue& after)
        {
            if (p.id == ColorSpectrum::colorProperty().id)
                if (const auto owner = weak.lock())
                {
                    auto& picker = static_cast<ColorPicker&>(*owner);
                    if (!picker.m_syncing)
                        picker.setColor(static_cast<u32>(after.asInt()));
                }
        });
    m_listeners.emplace_back(m_spectrum, token);
    for (usize i = 0; i < 5; ++i)
    {
        const auto slider =
            std::dynamic_pointer_cast<UIElement>(ui.tree().object(part("Channel" + std::to_string(i))));
        const auto listener = slider->addPropertyChanged(
            [weak, i](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&,
                      const PropertyValue&)
            {
                if (p.id == elementProperties().props.Value->id)
                    if (const auto owner = weak.lock())
                        static_cast<ColorPicker&>(*owner).channelChanged(i);
            });
        m_listeners.emplace_back(slider, listener);
    }
    m_events.push_back(ui.subscribeEvent(part("HexTextBox"), ui.events().LostFocus,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                                 static_cast<ColorPicker&>(*owner).commitHex();
                                         }));
    m_events.push_back(ui.subscribeEvent(part("HexTextBox"), ui.events().KeyDown,
                                         [weak](ElementId, RoutedEventArgs& routed)
                                         {
                                             const auto owner = weak.lock();
                                             auto* args = dynamic_cast<KeyEventArgs*>(&routed);
                                             if (!owner || !args)
                                                 return;
                                             auto& picker = static_cast<ColorPicker&>(*owner);
                                             if (args->key == Key::kEnter)
                                             {
                                                 args->handled = true;
                                                 picker.commitHex();
                                             }
                                             else if (args->key == Key::kEscape)
                                             {
                                                 args->handled = true;
                                                 picker.syncParts();
                                             }
                                         }));
    syncParts();
}
void ColorPicker::channelChanged(usize channel)
{
    if (m_syncing || !m_ui)
        return;
    const auto value =
        m_ui->tree().property(part("Channel" + std::to_string(channel)), *m_ui->props().Value).asDouble();
    if (channel == 4)
    {
        auto hsv = rgbToHsv(color());
        hsv.value = value / 100;
        setHsv(hsv, (color() >> 24) / 255.0);
    }
    else
    {
        const u32 shift = channel == 0 ? 16 : channel == 1 ? 8 : channel == 2 ? 0 : 24;
        setColor((color() & ~(255u << shift)) | (static_cast<u32>(std::lround(value)) << shift));
    }
}
void ColorPicker::syncParts()
{
    if (!m_ui || !part("Preview") || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* ui = m_ui;
    m_syncing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_syncing};
    ui->set(part("Preview"), ui->props().Background, static_cast<i64>(color()));
    if (m_ui != ui)
        return;
    ui->set(part("Preview"), ui->props().Visibility, getValue(property("IsColorPreviewVisible")));
    if (m_ui != ui)
        return;
    ui->set(part("SpectrumHost"), ui->props().Visibility, getValue(property("IsColorSpectrumVisible")));
    if (m_ui != ui)
        return;
    if (m_spectrum)
        m_spectrum->setColor(color());
    for (usize i = 0; i < 5 && m_ui == ui; ++i)
    {
        const f64 value = i == 4 ? rgbToHsv(color()).value * 100
                                 : static_cast<f64>((color() >> (i == 0   ? 16
                                                                 : i == 1 ? 8
                                                                 : i == 2 ? 0
                                                                          : 24)) &
                                                    255);
        ui->set(part("Channel" + std::to_string(i)), ui->props().Value, value);
        if (m_ui != ui)
            return;
        ui->set(part("ChannelRow" + std::to_string(i)), ui->props().Visibility,
                i != 3 || getValue(property("IsAlphaEnabled")).asBool());
    }
    if (m_ui != ui)
        return;
    ui->set(part("HexTextBox"), ui->props().Text, hex());
    if (m_ui == ui)
        ui->set(part("HexTextBox"), ui->props().Visibility, getValue(property("IsHexInputVisible")));
}
void ColorPicker::onDetached() noexcept
{
    for (const auto& [weak, token] : m_listeners)
        if (const auto object = weak.lock())
            object->removePropertyChanged(token);
    m_listeners.clear();
    m_spectrum.reset();
    FluentElement::onDetached();
}
u64 ColorPicker::addColorChanged(std::function<void(u32, u32)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p, const PropertyValue& a,
                                       const PropertyValue& b)
        {
            if (p.id == colorProperty().id)
                handler(static_cast<u32>(a.asInt()), static_cast<u32>(b.asInt()));
        });
}
} // namespace velora
