#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/globals.hpp>

namespace velora
{

// Receives immutable snapshots. Implementations run on the thread that owns the graphics
// device.
class FrameSink
{
public:
    virtual ~FrameSink() = default;
    virtual bool present(const SceneSnapshot& scene, const std::string* capturePath) = 0;
};

struct SchedulerTelemetry
{
    u64 ticks = 0;
    u64 presentedFrames = 0;
    u64 idleTicks = 0;       // ticks that submitted nothing
    u64 animationFrames = 0; // ticks that ran at least one animation callback
    u64 captures = 0;
    u64 failedPresents = 0;
};

// Drives animation callbacks, snapshotting and presentation from the UI thread.
// A tick submits a frame only when the compositor is dirty or a capture is pending,
// so a settled scene never presents.
class FrameScheduler
{
public:
    using AnimationId = u64;
    // Called once per tick with the frame time; return false when finished.
    using AnimationCallback = std::function<bool(double nowSeconds)>;

    FrameScheduler(Compositor& compositor, FrameSink& sink) : m_compositor(compositor), m_sink(sink) {}

    AnimationId startAnimation(AnimationCallback callback);
    bool stopAnimation(AnimationId id);
    std::size_t activeAnimations() const noexcept
    {
        return m_animations.size();
    }
    // Presents the current scene once more with a capture path, without re-lowering a clean tree.
    void requestCapture(std::string path)
    {
        m_capture = std::move(path);
    }

    // True when the host should keep ticking instead of blocking on platform events.
    bool hasPendingWork() const noexcept
    {
        return m_compositor.needsFrame() || !m_animations.empty() || m_capture.has_value();
    }
    // Returns true when a frame was presented.
    bool tick(double nowSeconds);

    const SchedulerTelemetry& telemetry() const noexcept
    {
        return m_telemetry;
    }
    const SceneSnapshot* lastFrame() const noexcept
    {
        return m_last ? &*m_last : nullptr;
    }

private:
    struct Animation
    {
        AnimationId id;
        AnimationCallback callback;
    };
    Compositor& m_compositor;
    FrameSink& m_sink;
    AnimationId m_nextAnimation = 1;
    std::vector<Animation> m_animations;
    std::optional<std::string> m_capture;
    std::optional<SceneSnapshot> m_last;
    SchedulerTelemetry m_telemetry;
};

} // namespace velora
