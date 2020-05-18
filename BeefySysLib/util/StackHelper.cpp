#include "StackHelper.h"
#include "BeefPerf.h"
#include "ThreadPool.h"

USING_NS_BF;

// Debug code takes more stack space - allow more there
#ifdef _DEBUG
#define MAX_STACK_SIZE 32*1024*1024
#else
#define MAX_STACK_SIZE 16*1024*1024
#endif

//#define MAX_STACK_SIZE 1*1024*1024 // Testing

static ThreadPool gThreadPool(8, MAX_STACK_SIZE);

StackHelper::StackHelper()
{

}

bool StackHelper::CanStackExpand(int wantBytes)
{	
	intptr stackBase = 0;
	int stackLimit = 0;
	BfpThreadResult threadResult;
	BfpThreadInfo_GetStackInfo(NULL, &stackBase, &stackLimit, BfpThreadInfoFlags_None, &threadResult);
	if (threadResult != BfpThreadResult_Ok)
		return true;

	//printf("StackInfo: %p %p %d\n", stackBase, &wantBytes, stackLimit);

	intptr expectedStackPtr = (intptr)(void*)&wantBytes - wantBytes;
	int resultSize = (int)(stackBase - expectedStackPtr);

	return resultSize <= stackLimit;
}

struct StackHelperExecuter
{
	const std::function<void()>* mFunc;
	SyncEvent mDoneEvent;

	static void BFP_CALLTYPE Proc(void* threadParam)
	{
		auto _this = (StackHelperExecuter*)threadParam;
		(*_this->mFunc)();
		_this->mDoneEvent.Set();
	}

	void Wait()
	{
		mDoneEvent.WaitFor();
	}
};

bool StackHelper::Execute(const std::function<void()>& func)
{
	// Only allow one threaded continuation
	if (gThreadPool.IsInJob())
		return false;

	StackHelperExecuter executer;
	executer.mFunc = &func;

	//printf("StackHelper executing!\n");

	gThreadPool.AddJob(StackHelperExecuter::Proc, (void*)&executer, 1);
	executer.Wait();
	return true;
}