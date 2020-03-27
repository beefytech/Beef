#include "Parallel.h"

// This is used to simplify the switch..case part
#define PAR_FUNCS_CASE(num,...)            \
case num:                                  \
concurrency::parallel_invoke(__VA_ARGS__); \
break

using namespace bf::System::Threading;

void Parallel::InvokeInternal(void* funcs, BF_INT_T count)
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
		concurrency::parallel_for(BF_INT(0), count - BF_INT(1), [&](BF_INT_T idx) {
			pFuncs[idx];
			});
		break;
	}
}

void Parallel::ForInternal(long long from, long long to, void* func)
{
	// According to C#, `from` is inclusive, `to` is exclusive 
	to -= 1;
	concurrency::parallel_for(from, to, (PForFunc)func);
}

void Parallel::ForeachInternal(void* arrOfPointers, BF_INT_T count, int elementSize, void* func)
{
	// A char is 1 byte
	char** refArr = (char**)arrOfPointers;
	PForeachFunc pf = (PForeachFunc)func;
	concurrency::parallel_for(BF_INT(0), count - BF_INT(1), [&](BF_INT_T idx) {
		pf(refArr + idx * elementSize);
		});
}
