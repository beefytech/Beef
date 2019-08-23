#include "../rt/Thread.h"
#include "DbgThread.h"

USING_NS_BF;

using namespace bf::System;
using namespace bf::System::Threading;

void Thread::Dbg_CreateInternal()
{
	BF_ASSERT((gBfRtFlags & BfRtFlags_LeakCheck) != 0);

	auto internalThread = new BfDbgInternalThread();
	SetInternalThread(internalThread);
}

BfDbgInternalThread::BfDbgInternalThread()
{
// 	mBFIThreadData = NULL;
// 	mTCMallocObjThreadCache = NULL;
// 	mReadCheckCount = 0;
// 	mLastGCScanIdx = 0;
// 	mLastStackScanIdx = 0;
// 	mSectionDepth = 0;
}

BfDbgInternalThread::~BfDbgInternalThread()
{

}

void BfDbgInternalThread::ThreadStarted()
{
	int threadPriority = BfpThread_GetPriority(mThreadHandle, NULL);
	mRunning = true;
	if ((gBfRtFlags & BfRtFlags_LeakCheck) != 0)
		gBFGC.ThreadStarted(this);
}

void BfDbgInternalThread::ThreadStopped()
{	
	mRunning = false;
	if ((gBfRtFlags & BfRtFlags_LeakCheck) != 0)
	{
		// Don't access thread after ThreadStopped -- the thread may be deleted
		gBFGC.ThreadStopped(this);
	}
}
