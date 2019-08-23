#include "gc.h"

#ifdef BF_PLATFORM_WINDOWS

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
 	switch (fdwReason)
 	{
 	case DLL_PROCESS_ATTACH:
 		gBFGC.ThreadStarted();
 		break;
 	case DLL_THREAD_ATTACH:
 		gBFGC.ThreadStarted();
 		break;
 	case DLL_THREAD_DETACH:
 		gBFGC.ThreadStopped();
 		break;
 	case DLL_PROCESS_DETACH:		
		gBFGC.ThreadStopped();
 		break;
 	}

	return TRUE;
}

#endif