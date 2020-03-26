#include "Parallel.h"

// parallel_invoke can accept 2-10 params, this is used to simplify the calling
#define PAR_INVOKE(...) concurrency::parallel_invoke(__VA_ARGS__)

// This is used to simplify the switch...case part
#define PAR_FUNCS_CASE(num,...) \
case num:                       \
PAR_INVOKE(__VA_ARGS__);        \
break

using namespace bf::System::Threading;

void Parallel::InvokeInternal(void* funcs, int count)
{
	BF_ASSERT((count > 1));

	PInvokeFunc* pFuncs = (PInvokeFunc*)funcs;
	switch (count)
	{
		PAR_FUNCS_CASE(2, pFuncs[0], pFuncs[1]);
		PAR_FUNCS_CASE(3, pFuncs[0], pFuncs[1], pFuncs[2]);
		PAR_FUNCS_CASE(4, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3]);
		PAR_FUNCS_CASE(5, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4]);
		PAR_FUNCS_CASE(6, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4], pFuncs[5]);
		PAR_FUNCS_CASE(7, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4], pFuncs[5], pFuncs[6]);
		PAR_FUNCS_CASE(8, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4], pFuncs[5], pFuncs[6], pFuncs[7]);
		PAR_FUNCS_CASE(9, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4], pFuncs[5], pFuncs[6], pFuncs[7], pFuncs[8]);
		PAR_FUNCS_CASE(10, pFuncs[0], pFuncs[1], pFuncs[2], pFuncs[3], pFuncs[4], pFuncs[5], pFuncs[6], pFuncs[7], pFuncs[8], pFuncs[9]);

	default:
		// TODO: Implement invoking 10+ functions with std::thread
		break;
	}
}

void Parallel::ForInternal(long long from, long long to, void* func)
{
	// According to C#, `from` is inclusive, `to` is exclusive 
	to -= 1;
	concurrency::parallel_for(from, to, (PForFuncLong)func);
}

void Parallel::ForInternal(int from, int to, void* func)
{
	// According to C#, `from` is inclusive, `to` is exclusive 
	to -= 1;
	concurrency::parallel_for(from, to, (PForFuncInt)func);
}

void Parallel::ForeachInternal(void* arrOfPointers, int count, void* func)
{
	// I must cast it to an int** here, because a void** cannot be used as a iterater
	int** refArr = (int**)arrOfPointers;
	concurrency::parallel_for_each(refArr, refArr + count, (PForeachFunc)func);
}
