#include "Parallel.h"

using namespace bf::System::Threading;

void Parallel::InvokeInternal(void* wrapper, void* func, BF_INT_T count)
{
	BF_ASSERT((count > 1));
	PInvokeFunc pFunc = (PInvokeFunc)func;

#if defined(_WIN32) || defined(_WIN64)
	concurrency::parallel_for(BF_INT(0), count, [&](BF_INT_T idx) {
		pFunc(wrapper, idx);
		});
#else
	BF_INT_T* indexes = new BF_INT_T[count];
	std::iota(indexes, indexes + count, 0);
	std::for_each(indexes, indexes + count, [&](BF_INT_T idx) {
		pFunc(wrapper, idx);
		});
	delete[] indexes;
#endif
}

void Parallel::ForInternal(long long from, long long to, void* wrapper, void* func)
{
	BF_ASSERT((from < to));
	PForFunc pFunc = (PForFunc)func;

#if defined(_WIN32) || defined(_WIN64)
	concurrency::parallel_for(from, to, [&](long long idx) {
		pFunc(wrapper, idx);
		});
#else
	long long* indexes = new long long[to - from];
	std::iota(indexes, indexes + (to - from), from);
	std::for_each(indexes, indexes + (to - from), [&](long long idx) {
		pFunc(wrapper, idx);
		});
	delete[] indexes;
#endif
}

void Parallel::ForInternal(long long from, long long to, void* meta, void* wrapper, void* func)
{
	PForFunc pFunc = (PForFunc)func;
	ParallelMetadata* pMeta = (ParallelMetadata*)meta;

	// TODO: Make it able to be canceled
	concurrency::parallel_for(from, to, [&](long long idx) {
		if (pMeta->running.load()) {
			pMeta->taskCount += 1;
			pFunc(wrapper, idx);
			pMeta->taskCount -= 1;
		}
		});
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
	// TODO: Canceling
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
