# Retained visual tree, layer cache, frame scheduling, and rendering it through Graphics.
velora_module(composition
    SOURCES velora/composition/Compositor.cpp velora/composition/FrameScheduler.cpp
        velora/composition/CompositionRenderer.cpp
    PUBLIC velora::graphics)
