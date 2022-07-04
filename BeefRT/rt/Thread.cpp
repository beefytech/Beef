#include "BeefySysLib/Common.h" 
#include "BfObjects.h"
#include "Thread.h"
//#include "ThreadLocalStorage.h"
#include "StompAlloc.h"
//#include <crtdbg.h>

//#define USE_STOMP_ALLOC

#undef MemoryBarrier

using namespace bf::System;
using namespace bf::System::Threading;

#ifdef BF_THREAD_TLS
BF_TLS_DECLSPEC Thread* Thread::sCurrentThread;
#endif

static volatile int gLiveThreadCount;
static Beefy::SyncEvent gThreadsDoneEvent;

#ifdef BF_PLATFORM_WINDOWS
extern DWORD gBfTLSKey;
#else
extern pthread_key_t gBfTLSKey;
#endif

bf::System::Threading::Thread* BfGetCurrentThread()
{
#ifdef BF_THREAD_TLS
	return Thread::sCurrentThread;
#else
    Thread* internalThread = (Thread*)BfpTLS_GetValue(BfTLSManager::sInternalThreadKey);
	return internalThread;
#endif	
}

void Thread::Suspend()
{
	BfpThread_Suspend(GetInternalThread()->mThreadHandle, NULL);	
}

void Thread::Resume()
{
	BfpThread_Resume(GetInternalThread()->mThreadHandle, NULL);	
}

void Thread::SetJoinOnDelete(bool joinOnDelete)
{
	auto internalThread = GetInternalThread();	
	Beefy::AutoCrit autoCrit(internalThread->mCritSect);	
	internalThread->mJoinOnDelete = joinOnDelete;			
}

int Thread::GetPriorityNative()
{
	return (int)BfpThread_GetPriority(GetInternalThread()->mThreadHandle, NULL) + 2;
}

void Thread::SetPriorityNative(int priority)
{
	return BfpThread_SetPriority(GetInternalThread()->mThreadHandle, (BfpThreadPriority)(priority - 2), NULL);	
}

bool Thread::GetIsAlive()
{
	if (GetInternalThread() == NULL)
		return false;
	bool success = BfpThread_WaitFor(GetInternalThread()->mThreadHandle, 0);
	return !success;
}

bool Thread::GetIsThreadPoolThread()
{
	return false;
}

bool Thread::JoinInternal(int millisecondsTimeout)
{
	auto internalThread = GetInternalThread();
	if (internalThread == NULL)
		return true;
	bool success = BfpThread_WaitFor(internalThread->mThreadHandle, millisecondsTimeout);	
	return success;
}

void Thread::SleepInternal(int millisecondsTimeout)
{
	BfpThread_Sleep(millisecondsTimeout);
}

void Thread::SpinWaitInternal(int iterations)
{
	BF_COMPILER_FENCE();
	BF_SPINWAIT_NOP();
}

bool Thread::YieldInternal()
{
	return BfpThread_Yield();
}

Thread* Thread::GetCurrentThreadNative()
{
    return BfGetCurrentThread();
}

unsigned long Thread::GetProcessDefaultStackSize()
{
	return 0;
}


static void BF_CALLTYPE CStartProc(void* threadParam)
{
	Thread* thread = (Thread*)threadParam;
#ifdef BF_THREAD_TLS
	Thread::sCurrentThread = thread;
#else
    BfpTLS_SetValue(BfTLSManager::sInternalThreadKey, thread);
#endif

	auto internalThread = thread->GetInternalThread();
	
	// Hold lock until we get ThreadStarted callback
	internalThread->mCritSect.Lock();

	internalThread->mStartedEvent.Set(true);
	internalThread->mThreadHandle = BfpThread_GetCurrent();
	internalThread->mStackStart = (intptr)&thread;
	internalThread->ThreadStarted();

	bool isAutoDelete = gBfRtCallbacks.Thread_IsAutoDelete(thread);	
	gBfRtCallbacks.Thread_ThreadProc(thread);
	bool isLastThread = BfpSystem_InterlockedExchangeAdd32((uint32*)&gLiveThreadCount, -1) == 1;

    //printf("Stopping thread\n");
    
	bool wantsDelete = false;
	//
	{	
		internalThread->ThreadStopped();		

		Beefy::AutoCrit autoCrit(internalThread->mCritSect);
		if (isAutoDelete)
			gBfRtCallbacks.Thread_AutoDelete(thread);

		internalThread->mDone = true;

		if (internalThread->mThread == NULL)
		{
			// If the thread was already deleted then we need to delete ourselves now			
			wantsDelete = true;			
		}
	}

	if (wantsDelete)
		delete internalThread;

	if (isLastThread)
		gThreadsDoneEvent.Set(false);

    //printf("Thread stopped\n");
}

void BfInternalThread::WaitForAllDone()
{	
	if ((gBfRtFlags & BfRtFlags_NoThreadExitWait) != 0)
		return;

	while (gLiveThreadCount != 0)
	{
		// Clear out any old done events
		gThreadsDoneEvent.WaitFor();		
	}
}

BfInternalThread* Thread::SetupInternalThread()
{
	BfInternalThread* internalThread;	
	internalThread = new BfInternalThread();
	SetInternalThread(internalThread);	
	return internalThread;
}

void Thread::ManualThreadInit()
{	
#ifdef BF_THREAD_TLS
	sCurrentThread = this;
#else
	BfpTLS_SetValue(BfTLSManager::sInternalThreadKey, this);
#endif
	
	BfInternalThread* internalThread = SetupInternalThread();
	internalThread->ManualThreadInit(this);
}

void Thread::StartInternal()
{	
	BfpSystem_InterlockedExchangeAdd32((uint32*)&gLiveThreadCount, 1);

	BfInternalThread* internalThread = SetupInternalThread();
	
	Beefy::AutoCrit autoCrit(internalThread->mCritSect);
	internalThread->mStarted = true;
	internalThread->mThread = this;
#ifdef _WIN32
	internalThread->mThreadHandle = BfpThread_Create(CStartProc, (void*)this, GetMaxStackSize(), (BfpThreadCreateFlags)(BfpThreadCreateFlag_StackSizeReserve | BfpThreadCreateFlag_Suspended), &internalThread->mThreadId);
	SetInternalThread(internalThread);				
	BfpThread_Resume(internalThread->mThreadHandle, NULL);
#else
	internalThread->mThreadHandle = BfpThread_Create(CStartProc, (void*)this, GetMaxStackSize(), (BfpThreadCreateFlags)(BfpThreadCreateFlag_StackSizeReserve), &internalThread->mThreadId);
	SetInternalThread(internalThread);	
#endif
}

void Thread::RequestExitNotify()
{
	// Do we already have implicit exiting notification?
	if (BfGetCurrentThread() != NULL)
		return;

#ifdef BF_PLATFORM_WINDOWS	
	FlsSetValue(gBfTLSKey, (void*)&gBfRtCallbacks);
#else		
	pthread_setspecific(gBfTLSKey, (void*)&gBfRtCallbacks);
#endif
}

void Thread::ThreadStarted()
{
	auto internalThread = GetInternalThread();
	internalThread->mCritSect.Unlock();
}

intptr Thread::GetThreadId()
{
	return GetInternalThread()->mThreadId;
}

void Thread::SetStackStart(void* ptr)
{
	GetInternalThread()->mRunning = true;
	GetInternalThread()->mStackStart = (intptr)ptr;
}

void Thread::InternalFinalize()
{
	auto internalThread = GetInternalThread();
	if (internalThread == NULL)
		return;
	
	bool wantsJoin = false;

	bool started = false;
	//
	{
		Beefy::AutoCrit autoCrit(internalThread->mCritSect);
		started = internalThread->mStarted;
	}

	if (started)
		internalThread->mStartedEvent.WaitFor();

	//
	{
		Beefy::AutoCrit autoCrit(internalThread->mCritSect);		
		if ((!internalThread->mDone) && (internalThread->mJoinOnDelete))
		{
			if (this != BfGetCurrentThread())
			{
				wantsJoin = true;				
			}
		}
	}

	if (wantsJoin)
		JoinInternal(0);

	bool wantsDelete = false;
	//	
	{
		Beefy::AutoCrit autoCrit(internalThread->mCritSect);
		
		if (!internalThread->mDone)
		{
			// We need to let the internal thread delete itself when it's done...
			internalThread->mThread = NULL;
		}
		else
		{			
			wantsDelete = true;				
		}		
		SetInternalThread(NULL);
	}

	if (internalThread->mIsManualInit)
		wantsDelete = true;

	if (wantsDelete)
		delete internalThread;	
}

bool Thread::IsBackgroundNative()
{
	return false;
}

void Thread::SetBackgroundNative(bool isBackground)
{

}

int Thread::GetThreadStateNative()
{
	return 0;
}

void Thread::InformThreadNameChange(String* name)
{	
	BfpThread_SetName(GetInternalThread()->mThreadHandle, (name != NULL) ? name->CStr() : "", NULL);
}

void Thread::MemoryBarrier()
{
	BF_FULL_MEMORY_FENCE();
}
