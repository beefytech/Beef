#include "BFPlatform.h"

#ifndef TCMALLOC_NAMESPACE
#define TCMALLOC_NAMESPACE tcmalloc
#endif

#ifdef BF_PLATFORM_WINDOWS
#ifdef _WIN64 
#define __x86_64__
#endif
#include "windows/config.h"
#include "windows/gperftools/tcmalloc.h"
#elif defined BF_PLATFORM_OSX
#include "osx/config.h"
#include "osx/gperftools/tcmalloc.h"
#elif defined BF_PLATFORM_IOS
#include "ios/config.h"
#include "ios/gperftools/tcmalloc.h"
#else
#error Platform not handled
#endif