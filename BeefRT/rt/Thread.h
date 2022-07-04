#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BfObjects.h"
#include "ThreadLocalStorage.h"

#ifdef BF_HAS_TLS_DECLSPEC
#define BF_THREAD_TLS
#endif

#pragma push_macro("MemoryBarrier")
#undef MemoryBarrier

class BfDbgInternalThread;

namespace bf
{
	namespace System
	{
		namespace Threading
		{			
			struct ThreadHandle
			{

			};

			class Thread : public Object
			{
			private:
				Thread();

			public:
				BF_DECLARE_CLASS(Thread, Object);

#ifdef BF_THREAD_TLS
				static BF_TLS_DECLSPEC Thread* sCurrentThread;
#endif
			private:
				BfInternalThread* SetupInternalThread();

				BFRT_EXPORT void ManualThreadInit();								
				BFRT_EXPORT int GetPriorityNative();
				BFRT_EXPORT void SetPriorityNative(int priority);
				BFRT_EXPORT void SetJoinOnDelete(bool joinOnDelete);
				BFRT_EXPORT bool GetIsAlive();
				BFRT_EXPORT bool GetIsThreadPoolThread();
				BFRT_EXPORT bool JoinInternal(int millisecondsTimeout);
				BFRT_EXPORT static void SleepInternal(int millisecondsTimeout);
				BFRT_EXPORT static void SpinWaitInternal(int iterations);
				BFRT_EXPORT static bool YieldInternal();
				BFRT_EXPORT static Thread* GetCurrentThreadNative();
				BFRT_EXPORT unsigned long GetProcessDefaultStackSize();								
				BFRT_EXPORT void StartInternal();				
				BFRT_EXPORT void ThreadStarted();
				BFRT_EXPORT void SetStackStart(void* ptr);
				BFRT_EXPORT void InternalFinalize();				
				BFRT_EXPORT bool IsBackgroundNative();
				BFRT_EXPORT void SetBackgroundNative(bool isBackground);
				BFRT_EXPORT int GetThreadStateNative();
				BFRT_EXPORT void InformThreadNameChange(String* name);				
				BFRT_EXPORT intptr GetThreadId();

				BFRT_EXPORT void Dbg_CreateInternal();

			public:
				BFRT_EXPORT void Suspend();
				BFRT_EXPORT void Resume();
				BFRT_EXPORT void Abort();

				BFRT_EXPORT static void RequestExitNotify();
				BFRT_EXPORT static void MemoryBarrier();

				static Thread* Alloc()
				{
					return BFRTCALLBACKS.Thread_Alloc();
				}

				BfInternalThread* GetInternalThread()
				{
					return BFRTCALLBACKS.Thread_GetInternalThread(this);
				}

				BfDbgInternalThread* Dbg_GetInternalThread()
				{
					return (BfDbgInternalThread*)BFRTCALLBACKS.Thread_GetInternalThread(this);
				}

				void SetInternalThread(BfInternalThread* internalThread)
				{
					BFRTCALLBACKS.Thread_SetInternalThread(this, internalThread);
				}

				int GetMaxStackSize()
				{
					return BFRTCALLBACKS.Thread_GetMaxStackSize(this);
				}
			};
		}
	} // Namespace System
}

class BfInternalThread
{
public:
	bool mIsSuspended;
	intptr mStackStart;	
	bf::System::Threading::Thread* mThread;	
	bool mRunning;
	bool mDone;
	bool mStarted;
	bool mJoinOnDelete;
	bool mIsManualInit;
	BfpThread* mThreadHandle;
	intptr mThreadId;	
	Beefy::CritSect mCritSect;
	Beefy::SyncEvent mStartedEvent;

	BfInternalThread()
	{
		mThread = NULL;
		mThreadHandle = 0;
		mStarted = false;
		mRunning = false;
		mDone = false;
		mIsSuspended = false;
		mJoinOnDelete = true;
		mIsManualInit = false;
		mStackStart = 0;
		mThreadId = 0;
	}

	virtual ~BfInternalThread()
	{
		if (mThreadHandle != 0)
		{
			BfpThread_Release(mThreadHandle);
		}
	}

	virtual void ManualThreadInit(bf::System::Threading::Thread* thread)
	{
		bf::System::Threading::Thread* newThread = thread;

		mIsManualInit = true;
		mStarted = true;
		mStartedEvent.Set(true);
		mThread = newThread;
		newThread->SetInternalThread(this);
		mThreadId = BfpThread_GetCurrentId();
		mThreadHandle = BfpThread_GetCurrent();
		mStackStart = ((intptr)&newThread + 0xFFF) & ~(intptr)0xFFF;
		ThreadStarted();
	}

	virtual void ThreadStarted()
	{		
		mRunning = true;
	}

	virtual void ThreadStopped()
	{
        //printf("TheadStopped\n");
		mRunning = false;
	}

	static void WaitForAllDone();
};

#pragma pop_macro("MemoryBarrier")
