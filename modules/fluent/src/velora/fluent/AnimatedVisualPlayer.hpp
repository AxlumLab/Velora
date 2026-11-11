// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/FluentElement.hpp>
#include <velora/graphics/Canvas.hpp>
namespace velora
{
// Ordinary C++ visual source, replacing the upstream Composition/WinRT source interface.
class AnimatedVisualSource
{
  public:
    virtual ~AnimatedVisualSource() = default;
    virtual Size intrinsicSize() const = 0;
    virtual f64 durationSeconds() const = 0;
    virtual void render(Canvas&, Rect destination, f64 progress) const = 0;
};
class AnimatedVisualPlayer final : public FluentElement
{
  public:
    AnimatedVisualPlayer();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& sourceProperty();
    std::shared_ptr<AnimatedVisualSource> source() const;
    PropertySetResult setSource(std::shared_ptr<AnimatedVisualSource>);
    void play(f64 from, f64 to, bool looped = false);
    void pause();
    void resume();
    void stop();
    PropertySetResult setProgress(f64);
    f64 progress() const;
    f64 duration() const;
    bool isPlaying() const noexcept;
    bool isAnimatedVisualLoaded() const;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    bool renderOverride(Canvas&, Size) const override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void startClock();
    bool advanceFrame(f64);
    void writeProgress(f64);
    void checkThread() const;
    u64 m_clock = 0;
    u64 m_generation = 0;
    f64 m_from = 0, m_to = 1, m_elapsed = 0;
    bool m_playing = false, m_looped = false, m_writing = false;
};
} // namespace velora
