#pragma once

#include "../sdl/SdlBFApp.h"
#include "../HeadlessApp.h"

NS_BF_BEGIN;

#ifdef BF_ENABLE_SDL
typedef SdlBFApp PlatformBFApp;
#else
typedef HeadlessApp PlatformBFApp;
#endif

NS_BF_END;