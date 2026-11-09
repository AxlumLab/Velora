// rkgen: -DVELORA_WINDOWS
#define FEATURE_AUDIO

namespace demo {

cclass Window
{
public:
#ifdef VELORA_WINDOWS
    cfield() void* hwnd;
#else
    cfield() void* surface;
#endif
#if defined(VELORA_LINUX)
    cfield() int x11;
#elif defined(VELORA_WINDOWS)
    cfield() int dpi;
#else
    cfield() int fallback;
#endif
    cfunc void Show();
};

cstruct Settings
{
    int width;
#ifndef FEATURE_AUDIO
    int silent;
#endif
#if FEATURE_AUDIO
    float volume;
#endif
    int height;
};

#if 0
cstruct Disabled
{
    int never;
};
#endif

cenum class Backend
{
    Software,
#ifdef VELORA_WINDOWS
    Direct3D,
#endif
    Vulkan,
};

}
