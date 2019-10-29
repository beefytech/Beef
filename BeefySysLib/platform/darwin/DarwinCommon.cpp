#include <execinfo.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#define lseek64 lseek
#define ftruncate64 ftruncate

#include "../posix/PosixCommon.cpp"

char* itoa(int value, char* str, int base)
{
    if (base == 16)
        sprintf(str, "%X", value);
    else
        sprintf(str, "%d", value);
    return str;
}
