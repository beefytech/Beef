#include "Common.h"

USING_NS_BF;

#ifdef BF_PLATFORM_SDL
#include "SDL2-2.0.1/include/SDL_main.h"
#endif

BF_EXPORT void BF_CALLTYPE Lib_Startup(int argc, const char** argv, void (*startupCallback)())
{
    gBFArgC = argc;
    gBFArgV = (char**)argv;
#ifdef SDL_MAIN_NEEDED
    extern SDL_bool SDL_MainIsReady;
    if (!SDL_MainIsReady)
    {
        gSDLStartupCallback = startupCallback;
        SDL_entry(gBFArgC, (char**)gBFArgV);
    }
    else
        startupCallback();
#else
        startupCallback();
#endif
}
