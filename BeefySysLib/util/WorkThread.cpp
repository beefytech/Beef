#include "WorkThread.h"

USING_NS_BF;

WorkThread::WorkThread()
{
	mThread = NULL;
}

WorkThread::~WorkThread()
{
	if (mThread != NULL)
		Stop();

	BfpThread_Release(mThread);
	mThread = NULL;
}

static void WorkThreadStub(void* param)
{
	((WorkThread*)param)->Run();
}

void WorkThread::Start()
{
	mThread = BfpThread_Create(WorkThreadStub, (void*)this, 256 * 1024, BfpThreadCreateFlag_StackSizeReserve);
}

void WorkThread::Stop()
{
	WaitForFinish();
}

void WorkThread::WaitForFinish()
{
	if (mThread == NULL)
		return;

    BfpThread_WaitFor(mThread, -1);
}

bool WorkThread::WaitForFinish(int waitMS)
{
	if (BfpThread_WaitFor(mThread, waitMS))
		return true;
	return false;
}

void WorkThreadFunc::Start(void(*func)(void*), void* param)
{
	mParam = param;
	mFunc = func;
	WorkThread::Start();
}

void WorkThreadFunc::Run()
{
	mFunc(mParam);
}
