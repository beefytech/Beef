#include "BFPlatform.h"

#ifdef BF_PLATFORM_WINDOWS
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