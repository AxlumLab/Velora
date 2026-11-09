#include <tuple>
#include <velora/globals.hpp>
// Framework: theme values, ControlTemplate instances, visual states and their animations.
#include "velora/runtime/Framework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <velora/composition/Compositor.hpp>

namespace velora
{
namespace
{

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

double easeIn(Easing::Kind kind, double t)
{
    switch (kind)
    {
    case Easing::Kind::kLinear:
        return t;
    case Easing::Kind::kQuadratic:
        return t * t;
    case Easing::Kind::kCubic:
        return t * t * t;
    case Easing::Kind::kQuartic:
        return t * t * t * t;
    case Easing::Kind::kQuintic:
        return t * t * t * t * t;
    case Easing::Kind::kSine:
        return 1 - std::cos(t * std::numbers::pi / 2);
    case Easing::Kind::kExponential:
        return (std::exp(2 * t) - 1) / (std::exp(2.0) - 1); // WinUI default Exponent=2
    case Easing::Kind::kCircle:
        return 1 - std::sqrt(std::max(0.0, 1 - t * t));
    }
    return t;
}

// Interpolates numbers, and colors per ARGB channel; other values switch at the end.
PropertyValue interpolate(const PropertyValue& a, const PropertyValue& b, double t)
{
    if (a.isDouble() && b.isDouble())
        return PropertyValue(a.asDouble() + (b.asDouble() - a.asDouble()) * t);
    if (a.isInt() && b.isInt())
    {
        i64 out = 0;
        for (int shift = 0; shift < 32; shift += 8)
        {
            const auto ca = static_cast<double>((a.asInt() >> shift) & 0xFF),
                       cb = static_cast<double>((b.asInt() >> shift) & 0xFF);
            out |= static_cast<i64>(std::lround(ca + (cb - ca) * t)) << shift;
        }
        return PropertyValue(out);
    }
    return t < 1 ? a : b;
}

} // namespace

double Easing::apply(double t) const
{
    t = std::clamp(t, 0.0, 1.0);
    switch (mode)
    {
    case Mode::kIn:
        return easeIn(kind, t);
    case Mode::kOut:
        return 1 - easeIn(kind, 1 - t);
    case Mode::kInOut:
        return t < 0.5 ? easeIn(kind, 2 * t) / 2 : 1 - easeIn(kind, 2 - 2 * t) / 2;
    }
    return t;
}

// ---- Themes ---------------------------------------------------------------------------

void Framework::setThemed(ElementId element, const PropertyDescriptor* property, Themed value,
                          PropertySource source)
{
    auto [it, first] = m_themed.try_emplace(element);
    if (first)
        m_tree.onRemoved(element, [this, element] { m_themed.erase(element); });
    auto& list = it->second;
    const auto existing = std::ranges::find_if(list, [&](const ThemedValue& v)
                                               { return v.property == property && v.source == source; });
    if (existing != list.end())
        existing->value = value;
    else
        list.push_back({property, source, value});
    set(element, property, value[m_theme], source);
}

// What shows where no element draws (WinUI ApplicationPageBackgroundThemeBrush): white in
// Light, black in Dark. A page with its own Background covers it.
void Framework::applyWindowBackground()
{
    m_compositor.setBackground(m_theme == Theme::kLight ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1}); // HC: black
}

void Framework::setTheme(Theme theme)
{
    if (theme == m_theme)
        return;
    m_theme = theme;
    applyWindowBackground();
    for (const auto& [element, list] : m_themed)
        for (const auto& v : list)
            set(element, v.property, v.value[m_theme], v.source);
    // Active visual states follow the theme, too.
    for (auto& [owner, states] : m_states)
        for (std::size_t g = 0; g < states.groups.size(); ++g)
            if (states.current[g] != kNone)
                for (const auto& s : states.groups[g].states[states.current[g]].setters)
                    set(s.target, s.property, s.value[m_theme], PropertySource::kAnimation);
    for (auto& run : m_running)
        evaluate(run, m_clock);
}

// ---- Templates ------------------------------------------------------------------------

void Framework::removeTemplate(ElementId owner)
{
    if (!m_tree.contains(owner))
        return;
    std::erase_if(m_running, [owner](const RunningTrack& run) { return run.owner == owner; });
    m_states.erase(owner);
    m_nameScopes.erase(owner);
    m_templateBindings.erase(owner);
    m_contentPresenters.erase(owner);
    if (auto it = m_templateRoots.find(owner); it != m_templateRoots.end())
    {
        const auto root = it->second;
        m_templateRoots.erase(it);
        m_tree.remove(root);
    }
    clear(owner, m_props.HasTemplate, PropertySource::kTemplate);
    clear(owner, m_visual.drawsBackground, PropertySource::kTemplate);
    contentChanged(owner);
}

void Framework::applyTemplate(ElementId owner, ElementId root,
                              std::vector<std::pair<std::string, ElementId>> names)
{
    if (auto it = m_templateRoots.find(owner); it != m_templateRoots.end() && it->second != root)
        m_tree.remove(it->second);
    if (auto it = m_templateBindings.find(owner); it != m_templateBindings.end())
        std::erase_if(it->second,
                      [this](const TemplateBinding& binding) { return !m_tree.contains(binding.target); });
    m_templateRoots[owner] = root;
    m_tree.onRemoved(owner,
                     [this, owner]
                     {
                         m_templateRoots.erase(owner);
                         m_contentPresenters.erase(owner);
                     });
    contentChanged(owner);
    set(owner, m_props.HasTemplate, PropertyValue(true), PropertySource::kTemplate);
    set(owner, m_visual.drawsBackground, PropertyValue(false), PropertySource::kTemplate);
    setNameScope(owner, std::move(names));
    // A copy: a handler may add or remove handlers, or apply another template.
    const auto handlers = m_templateApplied;
    for (const auto& [token, handler] : handlers)
        (*handler)(owner);
}

u64 Framework::addTemplateApplied(std::function<void(ElementId owner)> handler)
{
    if (!handler)
        return 0;
    const u64 token = m_nextTemplateToken++;
    m_templateApplied.emplace_back(token,
                                   std::make_shared<std::function<void(ElementId)>>(std::move(handler)));
    return token;
}

bool Framework::removeTemplateApplied(u64 token)
{
    return std::erase_if(m_templateApplied, [token](const auto& entry) { return entry.first == token; }) != 0;
}

void Framework::templateBind(ElementId target, const PropertyDescriptor* property, ElementId owner,
                             const PropertyDescriptor* ownerProperty)
{
    if (property == m_props.Content && ownerProperty == m_props.Content &&
        m_tree.is(target, ElementKind::kContentPresenter))
    {
        m_contentPresenters[owner] = target;
        m_tree.onRemoved(target,
                         [this, owner, target]
                         {
                             if (auto it = m_contentPresenters.find(owner);
                                 it != m_contentPresenters.end() && it->second == target)
                             {
                                 m_contentPresenters.erase(it);
                                 contentChanged(owner);
                             }
                         });
        contentChanged(owner);
    }

    auto [it, first] = m_templateBindings.try_emplace(owner);
    if (first)
        m_tree.onRemoved(owner, [this, owner] { m_templateBindings.erase(owner); });
    it->second.push_back({target, property, ownerProperty});
    set(target, property, m_tree.property(owner, *ownerProperty), PropertySource::kTemplate);
}

void Framework::setNameScope(ElementId owner, std::vector<std::pair<std::string, ElementId>> names)
{
    if (!m_tree.contains(owner))
        return;
    if (!m_nameScopes.contains(owner))
        m_tree.onRemoved(owner, [this, owner] { m_nameScopes.erase(owner); });
    m_nameScopes[owner] = std::move(names);
}

ElementId Framework::templateChild(ElementId owner, std::string_view name) const
{
    const auto it = m_nameScopes.find(owner);
    if (it == m_nameScopes.end())
        return 0;
    for (const auto& [n, id] : it->second)
        if (n == name)
            return id;
    return 0;
}

// ---- Visual states --------------------------------------------------------------------

void Framework::setVisualStates(ElementId owner, std::vector<VisualStateGroup> groups)
{
    std::erase_if(m_running, [&](const RunningTrack& r) { return r.owner == owner; });
    if (!m_states.contains(owner))
        m_tree.onRemoved(owner,
                         [this, owner]
                         {
                             std::erase_if(m_running,
                                           [&](const RunningTrack& r) { return r.owner == owner; });
                             m_states.erase(owner);
                         });
    auto& s = m_states[owner];
    s.current.assign(groups.size(), kNone);
    s.groups = std::move(groups);
    updateCommonState(owner);
}

bool Framework::goToState(ElementId owner, std::string_view state)
{
    const auto it = m_states.find(owner);
    if (it == m_states.end())
        return false;
    for (std::size_t g = 0; g < it->second.groups.size(); ++g)
        for (std::size_t s = 0; s < it->second.groups[g].states.size(); ++s)
            if (it->second.groups[g].states[s].name == state)
            {
                enterState(owner, it->second, g, s);
                return true;
            }
    return false;
}

std::string_view Framework::currentState(ElementId owner, std::string_view group) const
{
    const auto it = m_states.find(owner);
    if (it == m_states.end())
        return {};
    for (std::size_t g = 0; g < it->second.groups.size(); ++g)
        if (it->second.groups[g].name == group && it->second.current[g] != kNone)
            return it->second.groups[g].states[it->second.current[g]].name;
    return {};
}

void Framework::enterState(ElementId owner, StateOwner& states, std::size_t group, std::size_t state)
{
    if (states.current[group] == state)
        return;
    // A generated transition: remember the values the change touches before it happens.
    const auto& g = states.groups[group];
    const std::string_view fromName = states.current[group] != kNone
                                          ? std::string_view(g.states[states.current[group]].name)
                                          : std::string_view();
    const std::string_view toName = g.states[state].name;
    const VisualTransition* transition = nullptr;
    int best = -1;
    for (const auto& t : g.transitions)
    {
        if ((!t.from.empty() && t.from != fromName) || (!t.to.empty() && t.to != toName))
            continue;
        const int rank = (t.to.empty() ? 0 : 2) + (t.from.empty() ? 0 : 1);
        if (rank > best)
            best = rank, transition = &t;
    }
    std::vector<std::tuple<ElementId, const PropertyDescriptor*, PropertyValue>> before;
    if (transition && transition->duration > 0)
    {
        const auto note = [&](ElementId e, const PropertyDescriptor* d)
        {
            for (const auto& [te, td, v] : before)
                if (te == e && td == d)
                    return;
            if (m_tree.contains(e))
                before.emplace_back(e, d, m_tree.property(e, *d));
        };
        if (states.current[group] != kNone)
            for (const auto& s : g.states[states.current[group]].setters)
                note(s.target, s.property);
        for (const auto& s : g.states[state].setters)
            note(s.target, s.property);
    }
    // A transition still running in this group stops where it is.
    std::erase_if(m_running,
                  [&](const RunningTrack& r)
                  {
                      return r.owner == owner &&
                             std::ranges::any_of(states.generated,
                                                 [&](const AnimationTrack& t) { return &t == r.track; });
                  });
    states.generated.clear();
    if (const auto old = states.current[group]; old != kNone)
    {
        const auto& previous = states.groups[group].states[old];
        for (const auto& s : previous.setters)
            m_tree.clearProperty(s.target, *s.property, PropertySource::kAnimation);
        for (const auto& t : previous.storyboard)
            m_tree.clearProperty(t.target, *t.property, PropertySource::kAnimation);
        std::erase_if(m_running,
                      [&](const RunningTrack& r)
                      {
                          return r.owner == owner && r.track >= previous.storyboard.data() &&
                                 r.track < previous.storyboard.data() + previous.storyboard.size();
                      });
    }
    states.current[group] = state;
    const auto& next = states.groups[group].states[state];
    for (const auto& s : next.setters)
        set(s.target, s.property, s.value[m_theme], PropertySource::kAnimation);
    for (const auto& t : next.storyboard)
    {
        m_running.push_back({owner, &t, m_tree.property(t.target, *t.property), m_clock});
        evaluate(m_running.back(), m_clock);
    }
    // Generated transition tracks: from the value before the change to the value after it, for
    // the values the new state's storyboard does not animate itself.
    for (auto& [e, d, from] : before)
    {
        const PropertyValue to = m_tree.property(e, *d);
        const bool numeric = (from.isDouble() && to.isDouble()) || (from.isInt() && to.isInt());
        if (from == to || !numeric ||
            std::ranges::any_of(next.storyboard,
                                [&](const AnimationTrack& t) { return t.target == e && t.property == d; }))
            continue;
        const bool release = std::ranges::none_of(next.setters, [&](const StateSetter& s)
                                                  { return s.target == e && s.property == d; });
        KeyFrame frame{transition->duration, Themed{to, to},
                       transition->easing ? KeyFrame::Interpolation::kEased
                                          : KeyFrame::Interpolation::kLinear};
        if (transition->easing)
            frame.easing = *transition->easing;
        auto& track = states.generated.emplace_back(AnimationTrack{e, d, std::nullopt, {frame}, 0});
        m_running.push_back({owner, &track, from, m_clock, false, release});
        evaluate(m_running.back(), m_clock);
    }
}

void Framework::evaluate(RunningTrack& run, double now)
{
    const auto& track = *run.track;
    const double t = now - run.begin - track.begin;
    if (t < 0)
    {
        run.done = false;
        return;
    }
    PropertyValue previous = track.from ? (*track.from)[m_theme] : run.start;
    double previousTime = 0;
    for (const auto& frame : track.frames)
    {
        const PropertyValue& value = frame.value[m_theme];
        if (t < frame.time)
        {
            PropertyValue current = previous;
            if (frame.interpolation != KeyFrame::Interpolation::kDiscrete && frame.time > previousTime)
            {
                double progress = (t - previousTime) / (frame.time - previousTime);
                if (frame.interpolation == KeyFrame::Interpolation::kEased)
                    progress = frame.easing.apply(progress);
                current = interpolate(previous, value, progress);
            }
            set(track.target, track.property, std::move(current), PropertySource::kAnimation);
            run.done = false;
            return;
        }
        previous = value;
        previousTime = frame.time;
    }
    if (run.release)
        m_tree.clearProperty(track.target, *track.property, PropertySource::kAnimation); // back to the base
    else
        set(track.target, track.property, std::move(previous), PropertySource::kAnimation); // HoldEnd
    run.done = true;
}

void Framework::advance(double seconds)
{
    m_clock = seconds;
    for (std::size_t i = 0; i < m_running.size(); ++i)
        if (!m_running[i].done)
            evaluate(m_running[i], m_clock);
}

bool Framework::animating() const noexcept
{
    return std::ranges::any_of(m_running, [](const RunningTrack& r) { return !r.done; });
}

void Framework::updateCommonState(ElementId element)
{
    const auto it = m_states.find(element);
    if (it == m_states.end())
        return;
    const char* name = !m_tree.property(element, *m_props.IsEnabled).asBool()      ? "Disabled"
                       : m_tree.property(element, *m_props.IsPressed).asBool()     ? "Pressed"
                       : m_tree.property(element, *m_props.IsPointerOver).asBool() ? "PointerOver"
                                                                                   : "Normal";
    for (std::size_t g = 0; g < it->second.groups.size(); ++g)
    {
        if (it->second.groups[g].name != "CommonStates")
            continue;
        for (std::size_t s = 0; s < it->second.groups[g].states.size(); ++s)
            if (it->second.groups[g].states[s].name == name)
                enterState(element, it->second, g, s);
    }
}

} // namespace velora
