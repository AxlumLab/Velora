#include "velora/composition/FrameScheduler.hpp"

#include <algorithm>
#include <utility>

namespace velora
{

FrameScheduler::AnimationId FrameScheduler::startAnimation(AnimationCallback callback)
{
    const AnimationId id = m_nextAnimation++;
    m_animations.push_back({id, std::move(callback)});
    return id;
}

bool FrameScheduler::stopAnimation(AnimationId id)
{
    return std::erase_if(m_animations, [&](const Animation& a) { return a.id == id; }) > 0;
}

bool FrameScheduler::tick(double nowSeconds)
{
    ++m_telemetry.ticks;
    if (!m_animations.empty())
    {
        ++m_telemetry.animationFrames;
        // Callbacks may start or stop animations; iterate over a stable copy of the ids.
        std::vector<AnimationId> ids;
        ids.reserve(m_animations.size());
        for (const auto& a : m_animations)
            ids.push_back(a.id);
        for (const AnimationId id : ids)
        {
            const auto it = std::ranges::find(m_animations, id, &Animation::id);
            if (it == m_animations.end())
                continue;
            auto callback = it->callback;
            if (!callback(nowSeconds))
                stopAnimation(id);
        }
    }

    if (m_compositor.needsFrame())
        m_last = m_compositor.snapshot();
    else if (!m_capture || !m_last)
    {
        ++m_telemetry.idleTicks;
        return false;
    }

    const std::optional<std::string> capture = std::exchange(m_capture, std::nullopt);
    if (!m_sink.present(*m_last, capture ? &*capture : nullptr))
    {
        ++m_telemetry.failedPresents;
        return false;
    }
    ++m_telemetry.presentedFrames;
    if (capture)
        ++m_telemetry.captures;
    return true;
}

} // namespace velora
