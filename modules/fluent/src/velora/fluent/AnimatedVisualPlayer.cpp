// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// Play ranges, cancellation, pause/resume and source replacement adapted from
// WinUI 2.3 AnimatedVisualPlayer.cpp. Rendering uses Velora Canvas and host frame clocks.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/AnimatedVisualPlayer.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kType = "Velora.Fluent.AnimatedVisualPlayer";
}
AnimatedVisualPlayer::AnimatedVisualPlayer() : FluentElement(kType, "AnimatedVisualPlayer")
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == sourceProperty().id)
            {
                stop();
                if (m_ui && source() && getValue(property("AutoPlay")).asBool())
                    play(0, 1, true);
            }
            else if (p.id == property("Progress").id && !m_writing)
                pause();
            else if (p.id == property("AutoPlay").id && m_ui)
            {
                if (source() && getValue(property("AutoPlay")).asBool())
                    play(0, 1, true);
                else
                    pause();
            }
        });
}
const XamlTypeDeclaration& AnimatedVisualPlayer::declaration()
{
    return fluentDeclaration(kType);
}
std::string AnimatedVisualPlayer::registerType(Framework& ui)
{
    return registerFluentControl(ui, kType, [] { return createElement<AnimatedVisualPlayer>(); });
}
const PropertyDescriptor& AnimatedVisualPlayer::sourceProperty()
{
    static PropertyRegistry registry;
    static const auto& p = registry.define("AnimatedVisualPlayer.Source", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
std::shared_ptr<AnimatedVisualSource> AnimatedVisualPlayer::source() const
{
    const auto& ref = getValue(sourceProperty()).asObject();
    return ref ? std::shared_ptr<AnimatedVisualSource>{ref.owner,
                                                       static_cast<AnimatedVisualSource*>(ref.object)}
               : nullptr;
}
PropertySetResult AnimatedVisualPlayer::setSource(std::shared_ptr<AnimatedVisualSource> value)
{
    return setValue(sourceProperty(), ObjectRef::share<AnimatedVisualSource>(std::move(value)));
}
bool AnimatedVisualPlayer::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == sourceProperty().id)
    {
        if (!v.isObject())
            return false;
        const auto& ref = v.asObject();
        if (!ref)
            return true;
        if (!ref.owner || ref.owner.get() != ref.object || !ref.type ||
            *ref.type != typeid(AnimatedVisualSource))
            return false;
        const auto* source = static_cast<AnimatedVisualSource*>(ref.object);
        const auto size = source->intrinsicSize();
        return std::isfinite(source->durationSeconds()) && source->durationSeconds() > 0 &&
               std::isfinite(size.width) && std::isfinite(size.height) && size.width > 0 && size.height > 0;
    }
    if (p.id == property("Progress").id)
        return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() >= 0 && v.asDouble() <= 1;
    if (p.id == property("PlaybackRate").id)
        return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() > 0 && v.asDouble() <= 1024;
    if (p.id == property("Stretch").id)
        return v.isString() &&
               (v.asString() == "Uniform" || v.asString() == "UniformToFill" || v.asString() == "Fill");
    return FluentElement::validatePropertyValue(p, v);
}
void AnimatedVisualPlayer::checkThread() const
{
    if (!isOwnerThread())
        throw std::logic_error("AnimatedVisualPlayer belongs to its UI thread");
}
f64 AnimatedVisualPlayer::progress() const
{
    return getValue(property("Progress")).asDouble();
}
f64 AnimatedVisualPlayer::duration() const
{
    const auto visual = source();
    return visual ? visual->durationSeconds() : 0;
}
bool AnimatedVisualPlayer::isPlaying() const noexcept
{
    return m_playing;
}
bool AnimatedVisualPlayer::isAnimatedVisualLoaded() const
{
    return source() != nullptr;
}
void AnimatedVisualPlayer::writeProgress(f64 value)
{
    m_writing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_writing};
    setValue(property("Progress"), value);
}
PropertySetResult AnimatedVisualPlayer::setProgress(f64 value)
{
    checkThread();
    pause();
    return setValue(property("Progress"), value);
}
void AnimatedVisualPlayer::play(f64 from, f64 to, bool looped)
{
    checkThread();
    if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || from > 1 || to < 0 || to > 1)
        throw std::invalid_argument("Play range must be in [0,1]");
    pause();
    m_from = from;
    m_to = to;
    m_looped = looped;
    m_elapsed = 0;
    writeProgress(from);
    if (!source() || from == to)
    {
        writeProgress(to);
        return;
    }
    m_playing = true;
    startClock();
}
void AnimatedVisualPlayer::pause()
{
    checkThread();
    ++m_generation;
    m_playing = false;
    if (m_ui && m_clock)
        m_ui->removeAnimationClock(m_clock);
    m_clock = 0;
}
void AnimatedVisualPlayer::resume()
{
    checkThread();
    if (!source() || m_from == m_to)
        return;
    // setProgress can be followed by Resume: continue from the current position toward the last target.
    m_elapsed = std::abs(progress() - m_from) * duration();
    m_playing = true;
    startClock();
}
void AnimatedVisualPlayer::stop()
{
    checkThread();
    pause();
    m_elapsed = 0;
    writeProgress(0);
}
void AnimatedVisualPlayer::startClock()
{
    if (!m_ui || m_clock || !m_playing)
        return;
    const auto weak = std::weak_ptr<DependencyObject>(m_ui->tree().object(m_element));
    m_clock =
        m_ui->addAnimationClock(m_element,
                                [weak](f64 delta)
                                {
                                    if (const auto owner = weak.lock())
                                        return static_cast<AnimatedVisualPlayer&>(*owner).advanceFrame(delta);
                                    return false;
                                });
}
bool AnimatedVisualPlayer::advanceFrame(f64 delta)
{
    const auto generation = m_generation;
    if (!m_playing || !source())
    {
        m_clock = 0;
        return false;
    }
    const auto time = duration() * std::abs(m_to - m_from);
    m_elapsed += delta * getValue(property("PlaybackRate")).asDouble();
    const auto fraction = m_looped ? std::fmod(m_elapsed / time, 1.0) : std::min(1.0, m_elapsed / time);
    writeProgress(m_from + (m_to - m_from) * fraction);
    if (generation != m_generation)
        return false;
    if (!m_looped && fraction >= 1)
    {
        m_playing = false;
        m_clock = 0;
        return false;
    }
    return m_playing;
}
void AnimatedVisualPlayer::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    syncParts();
    if (source() && getValue(property("AutoPlay")).asBool())
        play(0, 1, true);
    else
        startClock();
}
void AnimatedVisualPlayer::onDetached() noexcept
{
    ++m_generation;
    if (m_ui && m_clock)
        m_ui->removeAnimationClock(m_clock);
    m_clock = 0;
    m_playing = false;
    FluentElement::onDetached();
}
void AnimatedVisualPlayer::syncParts()
{
    if (!m_ui || !part("FallbackText"))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* ui = m_ui;
    ui->set(part("FallbackText"), ui->props().Visibility, !source());
    if (m_ui == ui)
        ui->set(part("FallbackText"), ui->props().Text, getValue(property("FallbackText")));
}
bool AnimatedVisualPlayer::renderOverride(Canvas& canvas, Size size) const
{
    const auto visual = source();
    if (!visual)
        return false;
    const auto intrinsic = visual->intrinsicSize();
    const auto stretch = getValue(property("Stretch")).asString();
    Rect destination{0, 0, size.width, size.height};
    if (stretch != "Fill")
    {
        const f32 scale = stretch == "Uniform"
                              ? std::min(size.width / intrinsic.width, size.height / intrinsic.height)
                              : std::max(size.width / intrinsic.width, size.height / intrinsic.height);
        destination = {(size.width - intrinsic.width * scale) / 2,
                       (size.height - intrinsic.height * scale) / 2, intrinsic.width * scale,
                       intrinsic.height * scale};
    }
    canvas.save();
    canvas.clip({0, 0, size.width, size.height});
    visual->render(canvas, destination, progress());
    canvas.restore();
    return true;
}
} // namespace velora
