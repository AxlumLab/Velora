// velorac: VisualStateManager groups, setters and bounded storyboards.
#include "velora/xaml/CompilerImpl.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace velora
{

// ---- Visual states --------------------------------------------------------------------

std::optional<double> XamlCompiler::timespan(const XamlAttribute* a)
{
    if (!a)
        return 0.0;
    const std::string& t = a->value;
    if (t == "Forever" || t == "Automatic")
    {
        error(a->valueSpan,
              "'" + t + "' is not a bounded duration; animations need an explicit time such as 0:0:0.2");
        return std::nullopt;
    }
    double seconds = 0;
    std::size_t start = 0;
    int fields = 0;
    for (;;)
    {
        const auto colon = t.find(':', start);
        const auto part = parseNumber(t.substr(start, colon - start));
        if (!part || *part < 0)
        {
            error(a->valueSpan, "'" + t + "' is not a time span (use h:m:s, e.g. 0:0:0.25)");
            return std::nullopt;
        }
        seconds = seconds * 60 + *part;
        ++fields;
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    if (fields != 3 && !(fields == 1 && seconds == 0))
    {
        error(a->valueSpan, "'" + t + "' is not a time span (use h:m:s, e.g. 0:0:0.25)");
        return std::nullopt;
    }
    return seconds;
}

void XamlCompiler::visualStates(const XamlElement& groups)
{
    std::string code = "    m_ui.setVisualStates(m_owner, {\n";
    // The element and property a setter or animation targets, by template-local x:Name.
    struct Target
    {
        std::size_t index;
        const SchemaProperty* property;
    };
    const auto findTarget = [&](const std::string& name, const std::string& property,
                                const Span& span) -> std::optional<Target>
    {
        const auto it = m_scope->names.find(name);
        if (it == m_scope->names.end())
        {
            error(span, "'" + name + "' is not an x:Name in this ControlTemplate");
            return std::nullopt;
        }
        const auto* type = findSchemaType(m_scope->nameTypes[name]);
        const auto* p = findSchemaProperty(property);
        if (!p || !type || !hasSchemaProperty(*type, property))
        {
            error(span, "'" + m_scope->nameTypes[name] + "' has no property '" + property + "' to animate");
            return std::nullopt;
        }
        return Target{it->second.second, p};
    };
    const auto themed = [&](const ThemedResourceValue& v)
    { return "velora::Themed{" + cppValue(v.values[0]) + ", " + cppValue(v.values[1]) + "}"; };
    const auto easing = [&](const XamlElement& animation) -> std::optional<std::string>
    {
        for (const XamlElement* p : objectChildren(animation))
        {
            if (!p->propertyElement || p->property != "EasingFunction")
                continue;
            const auto functions = objectChildren(*p);
            if (functions.size() != 1)
                break;
            const XamlElement& f = *functions[0];
            static const std::map<std::string, std::string, std::less<>> kinds = {
                {"QuadraticEase", "Quadratic"}, {"CubicEase", "Cubic"}, {"QuarticEase", "Quartic"},
                {"QuinticEase", "Quintic"},     {"SineEase", "Sine"},   {"ExponentialEase", "Exponential"},
                {"CircleEase", "Circle"}};
            const auto kind = kinds.find(f.name.local);
            if (kind == kinds.end())
            {
                error(f.nameSpan, "'" + f.name.local +
                                      "' is not supported; use Quadratic, Cubic, Quartic, Quintic, Sine, "
                                      "Exponential or Circle easing");
                return std::nullopt;
            }
            if (const auto* exponent = f.attribute("", "Exponent"); exponent && exponent->value != "2")
            {
                error(exponent->valueSpan, "ExponentialEase supports only Exponent=\"2\"");
                return std::nullopt;
            }
            std::string mode = "Out";
            if (const auto* m = f.attribute("", "EasingMode"))
            {
                if (m->value == "EaseIn")
                    mode = "In";
                else if (m->value == "EaseInOut")
                    mode = "InOut";
                else if (m->value != "EaseOut")
                {
                    error(m->valueSpan, "EasingMode is EaseIn, EaseOut or EaseInOut");
                    return std::nullopt;
                }
            }
            return "velora::Easing{velora::Easing::Kind::k" + kind->second + ", velora::Easing::Mode::k" +
                   mode + "}";
        }
        return std::string();
    };
    const auto bounded = [&](const XamlElement& animation)
    {
        bool ok = true;
        if (const auto* r = animation.attribute("", "RepeatBehavior"); r && r->value != "1x")
        {
            error(r->valueSpan,
                  "RepeatBehavior '" + r->value + "' is not supported: visual state animations are bounded");
            ok = false;
        }
        if (const auto* r = animation.attribute("", "AutoReverse"); r && r->value != "False")
        {
            error(r->valueSpan, "AutoReverse is not supported: visual state animations play once");
            ok = false;
        }
        return ok;
    };
    // One Storyboard child -> an AnimationTrack initializer.
    const auto track = [&](const XamlElement& a) -> std::optional<std::string>
    {
        const std::string& kind = a.name.local;
        if (kind.ends_with("ThemeAnimation"))
        {
            error(a.nameSpan, "'" + kind + "' (a theme animation) is not supported by the Velora runtime");
            return std::nullopt;
        }
        const bool keyframes = kind.ends_with("UsingKeyFrames");
        const std::string base = keyframes ? kind.substr(0, kind.size() - 14)
                                           : kind; // ObjectAnimation, DoubleAnimation, ColorAnimation
        if (base != "ObjectAnimation" && base != "DoubleAnimation" && base != "ColorAnimation")
        {
            error(a.nameSpan,
                  "'" + kind +
                      "' is not supported; use Double/Color animations or ObjectAnimationUsingKeyFrames");
            return std::nullopt;
        }
        if (base == "ObjectAnimation" && !keyframes)
        {
            error(a.nameSpan, "ObjectAnimation needs key frames (ObjectAnimationUsingKeyFrames)");
            return std::nullopt;
        }
        if (!bounded(a))
            return std::nullopt;
        const auto* name = a.attribute("", "Storyboard.TargetName");
        const auto* path = a.attribute("", "Storyboard.TargetProperty");
        if (!name || !path)
        {
            error(a.nameSpan, "an animation needs Storyboard.TargetName and Storyboard.TargetProperty");
            return std::nullopt;
        }
        const auto target = findTarget(name->value, targetProperty(path->value), path->valueSpan);
        if (!target)
            return std::nullopt;
        const ValueKind k = target->property->kind;
        if ((base == "DoubleAnimation" && !numeric(k)) ||
            (base == "ColorAnimation" && k != ValueKind::kBrush))
        {
            error(path->valueSpan, base + " cannot animate " + std::string(target->property->name) + " (" +
                                       std::string(kindName(k)) + ")");
            return std::nullopt;
        }
        const auto valueOf = [&](const XamlAttribute& v)
        { return literalOrResource(v, m_dictionary, *target->property); };
        std::string frames, from = "std::nullopt";
        const auto begin = timespan(a.attribute("", "BeginTime"));
        if (!begin)
            return std::nullopt;
        if (!keyframes)
        {
            if (const auto* by = a.attribute("", "By"))
            {
                error(by->span, "By is not supported; use From/To");
                return std::nullopt;
            }
            const auto* to = a.attribute("", "To");
            if (!to)
            {
                error(a.nameSpan, kind + " needs To");
                return std::nullopt;
            }
            const auto duration = a.attribute("", "Duration") ? timespan(a.attribute("", "Duration"))
                                                              : std::optional<double>(1.0);
            const auto ease = easing(a);
            const auto toValue = valueOf(*to);
            if (!duration || !ease || !toValue)
                return std::nullopt;
            if (const auto* f = a.attribute("", "From"))
            {
                const auto fromValue = valueOf(*f);
                if (!fromValue)
                    return std::nullopt;
                from = themed(*fromValue);
            }
            frames = std::format("{{{}, {}, velora::KeyFrame::Interpolation::k{}, {}}}", *duration,
                                 themed(*toValue), ease->empty() ? "Linear" : "Eased",
                                 ease->empty() ? "{}" : *ease);
        }
        else
        {
            double last = 0;
            for (const XamlElement* f : objectChildren(a))
            {
                if (f->propertyElement)
                {
                    error(f->nameSpan, "'" + f->owner + "." + f->property + "' is not supported here");
                    continue;
                }
                const std::string& fk = f->name.local;
                const std::string prefix = base == "ObjectAnimation"   ? "Object"
                                           : base == "DoubleAnimation" ? "Double"
                                                                       : "Color";
                std::string interpolation;
                if (fk == "Discrete" + prefix + "KeyFrame")
                    interpolation = "Discrete";
                else if (fk == "Linear" + prefix + "KeyFrame" && prefix != "Object")
                    interpolation = "Linear";
                else if (fk == "Easing" + prefix + "KeyFrame" && prefix != "Object")
                    interpolation = "Eased";
                else
                {
                    error(f->nameSpan, "'" + fk + "' is not supported in " + kind);
                    return std::nullopt;
                }
                const auto time = timespan(f->attribute("", "KeyTime"));
                const auto* v = f->attribute("", "Value");
                if (!time)
                    return std::nullopt;
                if (!v)
                {
                    error(f->nameSpan, fk + " needs a Value");
                    return std::nullopt;
                }
                if (*time < last)
                {
                    error(f->attribute("", "KeyTime")->valueSpan, "key frames must be in time order");
                    return std::nullopt;
                }
                last = *time;
                const auto value = valueOf(*v);
                const auto ease =
                    interpolation == "Eased" ? easing(*f) : std::optional<std::string>(std::string());
                if (!value || !ease)
                    return std::nullopt;
                frames += std::format("{}{{{}, {}, velora::KeyFrame::Interpolation::k{}, {}}}",
                                      frames.empty() ? "" : ", ", *time, themed(*value), interpolation,
                                      ease->empty() ? "{}" : *ease);
            }
        }
        return std::format("{{{}, p.{}, {}, {{{}}}, {}}}", elementExpr(target->index),
                           memberOf(target->property->name), from, frames, *begin);
    };

    for (const XamlElement* g : objectChildren(groups))
    {
        if (!presentation(*g, "VisualStateGroup"))
        {
            error(g->nameSpan, "VisualStateGroups contains VisualStateGroup elements");
            continue;
        }
        const auto* groupName = g->attribute(kXamlNs, "Name");
        code += "        {" + cppString(groupName ? groupName->value : "") + ", {\n";
        for (const XamlElement* s : objectChildren(*g))
        {
            if (s->propertyElement && s->property == "Transitions")
            {
                error(s->nameSpan, "VisualStateGroup.Transitions are not supported; a state's own storyboard "
                                   "animates the change");
                continue;
            }
            if (!presentation(*s, "VisualState"))
            {
                error(s->nameSpan, "a VisualStateGroup contains VisualState elements");
                continue;
            }
            const auto* stateName = s->attribute(kXamlNs, "Name");
            if (!stateName)
                error(s->nameSpan, "a VisualState needs an x:Name");
            std::string setters, tracks;
            for (const XamlElement* c : objectChildren(*s))
            {
                const bool storyboard =
                    presentation(*c, "Storyboard") || (c->propertyElement && c->property == "Storyboard");
                if (c->propertyElement && c->property == "Setters")
                {
                    for (const XamlElement* setter : objectChildren(*c))
                    {
                        const auto* target = setter->attribute("", "Target");
                        const auto* value = setter->attribute("", "Value");
                        const auto dot = target ? target->value.rfind('.') : std::string::npos;
                        if (!presentation(*setter, "Setter") || !target || !value || dot == std::string::npos)
                        {
                            error(setter->nameSpan,
                                  "a VisualState setter needs Target=\"Name.Property\" and a Value");
                            continue;
                        }
                        const auto t = findTarget(target->value.substr(0, dot), target->value.substr(dot + 1),
                                                  target->valueSpan);
                        if (!t)
                            continue;
                        const auto v = literalOrResource(*value, m_dictionary, *t->property);
                        if (!v)
                            continue;
                        setters +=
                            std::format("{}{{{}, p.{}, {}}}", setters.empty() ? "" : ", ",
                                        elementExpr(t->index), memberOf(t->property->name), themed(*v));
                    }
                }
                else if (storyboard)
                {
                    const XamlElement* board = c;
                    if (c->propertyElement)
                    {
                        const auto boards = objectChildren(*c);
                        if (boards.size() != 1 || !presentation(*boards[0], "Storyboard"))
                        {
                            error(c->nameSpan, "'VisualState.Storyboard' contains one Storyboard");
                            continue;
                        }
                        board = boards[0];
                    }
                    if (!bounded(*board))
                        continue;
                    for (const XamlElement* animation : objectChildren(*board))
                        if (const auto t = track(*animation))
                            tracks += (tracks.empty() ? "" : ", ") + *t;
                }
                else
                {
                    error(c->nameSpan,
                          "'" + (c->propertyElement ? c->owner + "." + c->property : c->name.local) +
                              "' is not supported in a VisualState");
                }
            }
            code += "            {" + cppString(stateName ? stateName->value : "") + ", {" + setters +
                    "}, {" + tracks + "}},\n";
        }
        code += "        }},\n";
    }
    m_scope->states.push_back(line(groups.nameSpan) + code + "    });\n");
}

} // namespace velora
