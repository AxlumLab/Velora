#include "velora/runtime/FrameworkInternal.hpp"

namespace velora
{
// ---- Painting -----------------------------------------------------------------------

void Framework::registerPainters()
{
    const auto stroke = [this](Canvas& canvas, ElementId e, float w, float h)
    {
        const Thickness b = thickness(e, m_props.BorderThickness);
        const auto& brush = m_tree.property(e, *m_props.BorderBrush);
        if (b.left <= 0 || brush.empty())
            return;
        const auto t = static_cast<float>(b.left);
        const auto radius = static_cast<float>(number(e, m_props.CornerRadius));
        canvas.drawRoundedRect({t / 2, t / 2, w - t, h - t}, radius, {argb(brush.asInt()), t, true});
    };
    const auto foreground = [this](ElementId e)
    {
        const auto& v = m_tree.property(e, *m_props.Foreground);
        return argb(v.empty() ? 0xE4000000 : v.asInt(),
                    m_tree.property(e, *m_props.IsEnabled).asBool() ? 1.0f : 0.4f);
    };
    auto& bridge = *m_bridge;
    bridge.setPainter(ElementKind::kTextBlock,
                      [=, this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
                      {
                          canvas.drawText({0, 0, w, h}, std::string(text(e, m_props.Text)),
                                          static_cast<float>(number(e, m_props.FontSize)), {foreground(e)},
                                          std::string(text(e, m_props.FontFamily)));
                      });
    const auto contentPainter = [=, this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
    {
        if (m_tree.property(e, *m_props.HasTemplate).asBool())
            return; // the template draws everything
        stroke(canvas, e, w, h);
        if (m_tree.property(e, *m_props.IsPressed).asBool())
            canvas.drawRoundedRect({0, 0, w, h}, static_cast<float>(number(e, m_props.CornerRadius)),
                                   {argb(0x18000000)});
        const auto font = static_cast<float>(number(e, m_props.FontSize));
        if (contentObject(e))
            return;
        const std::string label(text(e, m_props.Content));
        const bool icon = text(e, m_props.FontFamily) == "Segoe MDL2 Assets";
        const float tw =
            icon ? font * static_cast<f32>(std::count_if(label.begin(), label.end(),
                                                         [](unsigned char c) { return (c & 0xC0) != 0x80; }))
                 : m_measure(label, font);
        const float th = lineHeight(font);
        canvas.drawText({(w - tw) / 2, (h - th) / 2, tw + 2, th}, label, font, {foreground(e)},
                        std::string(text(e, m_props.FontFamily)));
    };
    bridge.setPainter(ElementKind::kButton, contentPainter);
    bridge.setPainter(ElementKind::kContentPresenter, contentPainter);
    bridge.setPainter(ElementKind::kBorder, contentPainter);
    bridge.setPainter(ElementKind::kPage, contentPainter);
    bridge.setPainter(ElementKind::kUserControl, contentPainter);
    bridge.setPainter(ElementKind::kScrollViewer, contentPainter);
    // ScrollViewer scroll bars over the content: collapsed thin panning indicators, or the
    // expanded bar (track, arrows, thumb) while the pointer is over it. Geometry is shared with
    // the input handling through scrollBar().
    bridge.setOverlayPainter(
        ElementKind::kScrollViewer,
        [this](Canvas& canvas, const ElementTree&, ElementId e, float, float)
        {
            for (const bool vertical : {true, false})
            {
                const auto parts = scrollBar(e, vertical);
                if (!parts.shown)
                    continue;
                const auto rect = [](const Box& b) { return Rect{b.x, b.y, b.width, b.height}; };
                if (parts.expanded)
                {
                    canvas.drawRect(rect(parts.bar), {argb(0xF0F9F9F9)});
                    // Arrow glyphs: small triangles pointing away from the track.
                    for (const auto& [box, towards] :
                         {std::pair{parts.decrease, -1.0f}, std::pair{parts.increase, 1.0f}})
                    {
                        const float cx = box.x + box.width / 2, cy = box.y + box.height / 2, s = 3;
                        std::vector<PathElement> arrow =
                            vertical
                                ? std::vector<PathElement>{{PathVerb::kMoveTo,
                                                            {cx - s, cy - towards * s / 2}},
                                                           {PathVerb::kLineTo,
                                                            {cx + s, cy - towards * s / 2}},
                                                           {PathVerb::kLineTo, {cx, cy + towards * s / 2}},
                                                           {PathVerb::kClose}}
                                : std::vector<PathElement>{
                                      {PathVerb::kMoveTo, {cx - towards * s / 2, cy - s}},
                                      {PathVerb::kLineTo, {cx - towards * s / 2, cy + s}},
                                      {PathVerb::kLineTo, {cx + towards * s / 2, cy}},
                                      {PathVerb::kClose}};
                        canvas.drawPath(std::move(arrow), {argb(0x9E000000)});
                    }
                }
                const float radius = std::min(parts.thumb.width, parts.thumb.height) / 2;
                canvas.drawRoundedRect(rect(parts.thumb), radius,
                                       {argb(parts.expanded ? 0x9E000000 : 0x72000000)});
            }
        });
    bridge.setPainter(
        ElementKind::kTextBox,
        [=, this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
        {
            stroke(canvas, e, w, h);
            const bool focused = m_tree.property(e, *m_props.IsFocused).asBool();
            if (focused)
                canvas.drawRect({1, h - 2, w - 2, 2}, {argb(kAccent)});
            const Thickness p = thickness(e, m_props.Padding);
            const auto font = static_cast<float>(number(e, m_props.FontSize));
            const float x = static_cast<float>(p.left), lh = lineHeight(font), y = (h - lh) / 2;
            const float width = std::max(0.0f, w - x - static_cast<float>(p.right));
            const Edit edit = editState(e);
            const std::string preedit(text(e, m_props.PreeditText));
            if (edit.text.empty() && preedit.empty())
            {
                canvas.drawText({x, y, width, lh}, std::string(text(e, m_props.PlaceholderText)), font,
                                {argb(0x99000000)});
                if (focused)
                    canvas.drawRect({x, y + 2, 1, lh - 4}, {foreground(e)});
                return;
            }
            const auto widthOf = [&](std::string_view s) { return m_measure(s, font); };
            const std::string before = edit.text.substr(0, edit.caret), after = edit.text.substr(edit.caret);
            if (focused && edit.caret != edit.anchor && preedit.empty())
            {
                const auto from = std::min(edit.caret, edit.anchor), to = std::max(edit.caret, edit.anchor);
                const std::string_view all(edit.text);
                canvas.drawRect(
                    {x + widthOf(all.substr(0, from)), y + 1, widthOf(all.substr(from, to - from)), lh - 2},
                    {argb(0x660078D4)});
            }
            canvas.drawText({x, y, width, lh}, before + preedit + after, font, {foreground(e)});
            // Composition text is underlined; the caret follows the IME cursor inside it.
            if (!preedit.empty())
                canvas.drawRect({x + widthOf(before), y + lh - 2, widthOf(preedit), 1}, {foreground(e)});
            if (focused)
            {
                const auto cursor =
                    static_cast<std::size_t>(m_tree.property(e, *m_props.PreeditCursor).asInt());
                canvas.drawRect({x + widthOf(before + preedit.substr(0, codePointPrefix(preedit, cursor))),
                                 y + 2, 1, lh - 4},
                                {foreground(e)});
            }
        });
    // CheckBox / RadioButton: a 20-DIP glyph left of the text, vertically centred.
    const auto toggleGlyph = [=, this](Canvas& canvas, ElementId e, float h, bool round)
    {
        const bool enabled = m_tree.property(e, *m_props.IsEnabled).asBool();
        const bool checked = m_tree.property(e, *m_props.IsChecked).asBool();
        const bool pressed = m_tree.property(e, *m_props.IsPressed).asBool();
        const float left = static_cast<float>(thickness(e, m_props.Padding).left), top = (h - 20) / 2;
        const float radius = round ? 10.0f : 3.0f;
        const Rect glyph{left, top, 20, 20};
        if (checked)
        {
            const u32 fill = !enabled ? 0x37000000 : pressed ? 0xFF005A9E : static_cast<u32>(kAccent);
            if (round)
            {
                canvas.drawRoundedRect(glyph, radius, {argb(fill)});
                canvas.drawRoundedRect({left + 6, top + 6, 8, 8}, 4, {argb(0xFFFFFFFF)});
            }
            else
            {
                canvas.drawRoundedRect(glyph, radius, {argb(fill)});
                canvas.drawPath({{PathVerb::kMoveTo, {left + 5, top + 10}},
                                 {PathVerb::kLineTo, {left + 8.5f, top + 13.5f}},
                                 {PathVerb::kLineTo, {left + 15, top + 6.5f}}},
                                {argb(0xFFFFFFFF), 1.5f, true});
            }
        }
        else
        {
            if (pressed)
                canvas.drawRoundedRect(glyph, radius, {argb(0x33000000)});
            canvas.drawRoundedRect({left + 0.5f, top + 0.5f, 19, 19}, radius - 0.5f,
                                   {argb(enabled ? 0xFF6E6E6E : 0x37000000), 1, true});
        }
        const std::string label(text(e, m_props.Content));
        const auto font = static_cast<float>(number(e, m_props.FontSize));
        canvas.drawText({left + 28, (h - lineHeight(font)) / 2, m_measure(label, font) + 2, lineHeight(font)},
                        label, font, {foreground(e)});
    };
    bridge.setPainter(ElementKind::kCheckBox, [=](Canvas& canvas, const ElementTree&, ElementId e, float,
                                                  float h) { toggleGlyph(canvas, e, h, false); });
    bridge.setPainter(ElementKind::kRadioButton, [=](Canvas& canvas, const ElementTree&, ElementId e, float,
                                                     float h) { toggleGlyph(canvas, e, h, true); });
    // ToggleSwitch: header, then a 40x20 pill with its knob and the On/Off content.
    bridge.setPainter(
        ElementKind::kToggleSwitch,
        [=, this](Canvas& canvas, const ElementTree&, ElementId e, float, float h)
        {
            const bool enabled = m_tree.property(e, *m_props.IsEnabled).asBool();
            const bool on = m_tree.property(e, *m_props.IsOn).asBool();
            const auto font = static_cast<float>(number(e, m_props.FontSize));
            const std::string header(text(e, m_props.Header));
            float top = 0;
            if (!header.empty())
            {
                canvas.drawText({0, 0, m_measure(header, font) + 2, lineHeight(font)}, header, font,
                                {foreground(e)});
                top = lineHeight(font) + 4;
            }
            const float row = std::max(20.0f, h - top), y = top + (row - 20) / 2;
            if (on)
            {
                canvas.drawRoundedRect({0, y, 40, 20}, 10,
                                       {argb(enabled ? static_cast<u32>(kAccent) : 0x37000000)});
                canvas.drawRoundedRect({25, y + 5, 10, 10}, 5, {argb(0xFFFFFFFF)});
            }
            else
            {
                canvas.drawRoundedRect({0.5f, y + 0.5f, 39, 19}, 9.5f,
                                       {argb(enabled ? 0xFF6E6E6E : 0x37000000), 1, true});
                canvas.drawRoundedRect({5, y + 5, 10, 10}, 5, {argb(enabled ? 0xFF6E6E6E : 0x37000000)});
            }
            const std::string state(text(e, on ? m_props.OnContent : m_props.OffContent));
            canvas.drawText(
                {52, top + (row - lineHeight(font)) / 2, m_measure(state, font) + 2, lineHeight(font)}, state,
                font, {foreground(e)});
        });
    // Slider: a 4-DIP track filled up to the value, and a round thumb at the value.
    bridge.setPainter(
        ElementKind::kSlider,
        [this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
        {
            const bool enabled = m_tree.property(e, *m_props.IsEnabled).asBool();
            const double minimum = number(e, m_props.Minimum), maximum = number(e, m_props.Maximum);
            const double fraction =
                maximum > minimum ? (number(e, m_props.Value) - minimum) / (maximum - minimum) : 0;
            const float span = std::max(0.0f, w - 20), x = 10 + span * static_cast<float>(fraction),
                        y = h / 2;
            canvas.drawRoundedRect({10, y - 2, span, 4}, 2, {argb(0x66000000)});
            canvas.drawRoundedRect({10, y - 2, x - 10, 4}, 2,
                                   {argb(enabled ? static_cast<u32>(kAccent) : 0x37000000)});
            canvas.drawRoundedRect({x - 10, y - 10, 20, 20}, 10, {argb(0xFFFFFFFF)});
            canvas.drawRoundedRect({x - 9.5f, y - 9.5f, 19, 19}, 9.5f, {argb(0x29000000), 1, true});
            canvas.drawRoundedRect({x - 6, y - 6, 12, 12}, 6,
                                   {argb(enabled ? static_cast<u32>(kAccent) : 0x37000000)});
        });
    // ProgressBar: Background track and the Foreground fill up to the value.
    bridge.setPainter(ElementKind::kProgressBar,
                      [this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
                      {
                          const double minimum = number(e, m_props.Minimum),
                                       maximum = number(e, m_props.Maximum);
                          const double fraction =
                              maximum > minimum ? (number(e, m_props.Value) - minimum) / (maximum - minimum)
                                                : 0;
                          const auto& fill = m_tree.property(e, *m_props.Foreground);
                          canvas.drawRoundedRect({0, 0, w * static_cast<float>(fraction), h}, h / 2,
                                                 {argb(fill.empty() ? kAccent : fill.asInt())});
                      });
    for (const ElementKind kind : {ElementKind::kBorder, ElementKind::kGrid, ElementKind::kStackPanel,
                                   ElementKind::kListView, ElementKind::kCanvas})
        bridge.setPainter(kind, [=](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
                          { stroke(canvas, e, w, h); });
    bridge.setPainter(ElementKind::kImage,
                      [this](Canvas& canvas, const ElementTree&, ElementId e, float w, float h)
                      {
                          if (const auto source = text(e, m_props.Source); !source.empty())
                          {
                              const auto stretch = text(e, m_props.Stretch);
                              canvas.drawImage({0, 0, w, h}, std::string(source),
                                               stretch == "UniformToFill" ? ImageFit::kCover
                                               : stretch == "Fill"        ? ImageFit::kStretch
                                                                          : ImageFit::kContain);
                          }
                      });
}
} // namespace velora
