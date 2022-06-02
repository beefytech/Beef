#pragma once

extern "C" 
#ifdef BF_STOMP_EXPORT
__declspec(dllexport)
#endif
void* StompAlloc(intptr size);

extern "C" 
#ifdef BF_STOMP_EXPORT
__declspec(dllexport)
#endif
void StompFree(void* addr);
