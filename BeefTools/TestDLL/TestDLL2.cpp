#include "TestDLL.h"

extern "C"
__declspec(dllexport) void Test4(int a, int b)
{
	TestMe tm;
	tm.GetIt(222);
}