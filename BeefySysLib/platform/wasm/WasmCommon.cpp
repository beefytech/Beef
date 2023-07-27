#define lseek64 lseek
#define ftruncate64 ftruncate

#include "../posix/PosixCommon.cpp"

extern "C" uintptr_t _Unwind_GetIP(struct _Unwind_Context *context)
{
	return 0;
}
