#include "ThreadPool.h"
#include "BeefPerf.h"

USING_NS_BF;

static BF_TLS_DECLSPEC ThreadPool* gPoolParent;

ThreadPool::Thread::Thread()
{
	mCurJobThreadId = -1;
	mActiveJob = NULL;
}

ThreadPool::Thread::~Thread()
{
	BfpThread_Release(mBfpThread);
}

void ThreadPool::Thread::Proc()
{
	bool isWorking = true;

	BpSetThreadName("ThreadPoolProc");
	BfpThread_SetName(NULL, "ThreadPoolProc", NULL);

	gPoolParent = mThreadPool;

	while (true)
	{				
		Job* job = NULL;
		//
		{
			AutoCrit autoCrit(mThreadPool->mCritSect);
			if (!mThreadPool->mJobs.IsEmpty())
			{
				job = mThreadPool->mJobs[0];
				job->mProcessing = true;
				mThreadPool->mJobs.RemoveAt(0);				
			}
			
			mActiveJob = job;
			if (job == NULL)
				mCurJobThreadId = -1;
			else
				mCurJobThreadId = job->mFromThreadId;

			bool hasWork = job != NULL;
			if (hasWork != isWorking)
			{
				isWorking = hasWork;
				if (isWorking)
					mThreadPool->mFreeThreads--;
				else
					mThreadPool->mFreeThreads++;
			}
		}
		
		if ((job == NULL) && (mThreadPool->mShuttingDown))
		{
			break;
		}

		bool didCancel = false;
		if ((mThreadPool->mShuttingDown) && (job->Cancel()))
			didCancel = true;

		if (job == NULL)
		{
			mThreadPool->mEvent.WaitFor();
			continue;
		}

		if (!didCancel)
		{
			BP_ZONE("ThreadProc:Job");
			job->Perform();
		}

		// Run dtor synchronized
		AutoCrit autoCrit(mThreadPool->mCritSect);
		mActiveJob = NULL;
		delete job;
	}	
}

ThreadPool::ThreadPool(int maxThreads, int stackSize)
{
	mMaxThreads = maxThreads;
	mShuttingDown = false;
	mStackSize = stackSize;
	mFreeThreads = 0;
	mRunningThreads = 0;
}

ThreadPool::~ThreadPool()
{
	Shutdown();
}

void ThreadPool::Shutdown()
{
	mShuttingDown = true;
	mEvent.Set(true);

	while (true)
	{
		Thread* thread = NULL;
		//
		{
			AutoCrit autoCrit(mCritSect);
			if (mThreads.IsEmpty())
				break;
			thread = mThreads.back();
			mThreads.pop_back();
		}
		
		if (!BfpThread_WaitFor(thread->mBfpThread, -1))
			break;

		AutoCrit autoCrit(mCritSect);
		delete thread;		
		mRunningThreads--;				
	}	

	BF_ASSERT(mRunningThreads == 0);

	for (auto job : mJobs)
		delete job;
	mJobs.Clear();
}

void ThreadPool::SetStackSize(int stackSize)
{
	mStackSize = stackSize;
}

static void BFP_CALLTYPE WorkerProc(void* param)
{
	((ThreadPool::Thread*)param)->Proc();
}

void ThreadPool::AddJob(Job* job, int maxWorkersPerProviderThread)
{
	AutoCrit autoCrit(mCritSect);

	BfpThreadId curThreadId = BfpThread_GetCurrentId();
	
	job->mFromThreadId = curThreadId;
	mJobs.Add(job);
	mEvent.Set();	

	if (((int)mThreads.size() < mMaxThreads) && (mFreeThreads == 0))
	{
		int workersForUs = 0;
		for (auto thread : mThreads)
		{
			if (thread->mCurJobThreadId == curThreadId)
				workersForUs++;
		}
		if (workersForUs >= maxWorkersPerProviderThread)
			return;

		mRunningThreads++;
		Thread* thread = new Thread();
		thread->mThreadPool = this;
		thread->mBfpThread = BfpThread_Create(WorkerProc, (void*)thread, mStackSize, BfpThreadCreateFlag_StackSizeReserve);
		mThreads.Add(thread);
	}
}

void ThreadPool::AddJob(BfpThreadStartProc proc, void* param, int maxWorkersPerProviderThread)
{
	ProcJob* job = new ProcJob();
	job->mProc = proc;
	job->mParam = param;
	AddJob(job, maxWorkersPerProviderThread);
}

bool ThreadPool::IsInJob()
{
	return gPoolParent == this;
}

void ThreadPool::CancelAll()
{
	AutoCrit autoCrit(mCritSect);
	for (auto job : mJobs)
		job->Cancel();
	for (auto thread : mThreads)
		if (thread->mActiveJob != NULL)
			thread->mActiveJob->Cancel();
}