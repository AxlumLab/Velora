#pragma once
#include "DisplayList.hpp"

namespace velora
{
class Canvas final
{
  public:
    void clear(Color color)
    {
        m_commands.push_back(ClearCommand{color});
    }
    void drawRect(Rect bounds, Paint paint)
    {
        m_commands.push_back(RectCommand{bounds, paint});
    }
    void drawRoundedRect(Rect bounds, float radius, Paint paint)
    {
        m_commands.push_back(RoundedRectCommand{bounds, radius, paint});
    }
    void drawPath(std::vector<PathElement> elements, Paint paint)
    {
        m_commands.push_back(PathCommand{std::move(elements), paint});
    }
    void drawText(Rect bounds, std::string value, float fontSize, Paint paint,
                  std::string fontFamily = "Segoe UI")
    {
        m_commands.push_back(TextCommand{bounds, std::move(value), fontSize, paint, std::move(fontFamily)});
    }
    void drawImage(Rect bounds, std::string path, ImageFit fit = ImageFit::kContain)
    {
        m_commands.push_back(ImageCommand{bounds, std::move(path), fit});
    }
    void clip(Rect bounds)
    {
        m_commands.push_back(ClipRectCommand{bounds});
    }
    void clipRoundedRect(Rect bounds, float radius)
    {
        m_commands.push_back(ClipRoundedRectCommand{bounds, radius});
    }
    void transform(Transform value)
    {
        m_commands.push_back(TransformCommand{value});
    }
    void restore()
    {
        m_commands.push_back(RestoreCommand{});
    }
    void save()
    {
        m_commands.push_back(SaveCommand{});
    }
    void saveLayer(Rect bounds, float opacity)
    {
        m_commands.push_back(SaveLayerCommand{bounds, opacity});
    }
    void opacity(float value)
    {
        m_commands.push_back(OpacityCommand{value});
    }
    void unsupported(UnsupportedFeature feature)
    {
        m_commands.push_back(UnsupportedCommand{feature});
    }
    DisplayList finish() &&
    {
        return DisplayList{std::move(m_commands)};
    }

  private:
    std::vector<Command> m_commands;
};
} // namespace velora
