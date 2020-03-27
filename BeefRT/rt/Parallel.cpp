#include "Parallel.h"

// This is used to simplify the switch..case part
#define PAR_FUNCS_CASE(num,...)            \
case BF_INT(num):                          \
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
		concurrency::parallel_for(BF_INT(0), count, [&](BF_INT_T idx) {
			pFuncs[idx]();
			});
		break;
	}
}

void Parallel::ForInternal(long long from, long long to, void* wrapper, void* func)
{
	PForFunc pFunc = (PForFunc)func;
	concurrency::parallel_for(from, to, [&](long long idx) {
		pFunc(wrapper, idx);
		});
}

void Parallel::ForInternal(long long from, long long to, void* pState, void* meta, void* wrapper, void* func)
{
	PStatedForFunc pFunc = (PStatedForFunc)func;
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	pMeta->cts = concurrency::cancellation_token_source();

	concurrency::run_with_cancellation_token([&]() {
		concurrency::parallel_for(from, to, [&](long long idx) {
			if (pMeta->running.load()) {
				pMeta->taskCount += 1;
				pFunc(wrapper, idx, pState);
				pMeta->taskCount -= 1;
			}
			});
		}, pMeta->cts.get_token());
}

void Parallel::ForeachInternal(void* arrOfPointers, BF_INT_T count, int elementSize, void* wrapper, void* func)
{
	// A char is 1 byte
	char* refArr = (char*)arrOfPointers;
	PForeachFunc pf = (PForeachFunc)func;
	concurrency::parallel_for(BF_INT(0), count, [&](BF_INT_T idx) {
		pf(wrapper, refArr + idx * elementSize);
		});
}

void Parallel::ForeachInternal(void* arrOfPointers, BF_INT_T count, int elementSize, void* pState, void* meta, void* wrapper, void* func)
{
	char* refArr = (char*)arrOfPointers;
	PStatedForeachFunc pf = (PStatedForeachFunc)func;
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	pMeta->cts = concurrency::cancellation_token_source();

	concurrency::run_with_cancellation_token([&]() {
		concurrency::parallel_for(BF_INT(0), count, [&](BF_INT_T idx) {
			if (pMeta->running.load()) {
				pMeta->taskCount += 1;
				pf(wrapper, refArr + idx * elementSize, pState);
				pMeta->taskCount -= 1;
			}
			});
		}, pMeta->cts.get_token());
}

void ParallelState::InitializeMeta(void* meta)
{
	meta = new ParallelMetadata{};
}

void ParallelState::BreakInternal(void* meta)
{
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	pMeta->running.store(false);
}

void ParallelState::StopInternal(void* meta)
{
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	pMeta->cts.cancel();
}

bool ParallelState::StoppedInternal(void* meta)
{
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	return pMeta->running.load();
}

bool ParallelState::ShouldStopInternal(void* meta)
{
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;
	return pMeta->taskCount == 0;
}
