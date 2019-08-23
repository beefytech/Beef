#pragma once

#ifdef BF_FORCE_SDL
#include "SdlBFApp.h"
#else
#include "WinBFApp.h"
#endif

NS_BF_BEGIN;

#ifdef BF_FORCE_SDL
typedef SdlBFApp PlatformBFApp;
#else
typedef WinBFApp PlatformBFApp;
#endif

NS_BF_END;