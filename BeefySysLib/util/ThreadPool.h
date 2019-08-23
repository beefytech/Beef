#pragma once

#include "../Common.h"
#include "CritSect.h"
#include "Deque.h"

NS_BF_BEGIN

class ThreadPool
{
public:
	class Thread
	{
	public:
		ThreadPool* mThreadPool;
		BfpThread* mBfpThread;
		BfpThreadId mCurJobThreadId;

	public:
		Thread();
		~Thread();
		void Proc();
	};

	class Job
	{
	public:
		BfpThreadId mFromThreadId;
		bool mProcessing;

		Job()
		{
			mFromThreadId = 0;
			mProcessing = false;
		}

		// By default don't allow cancelling
		virtual bool Cancel()
		{
			return false;
		}

		virtual ~Job()
		{
		}

		virtual void Perform() = 0;		
	};

	class ProcJob : public Job
	{
	public:
		BfpThreadStartProc mProc;
		void* mParam;		

		ProcJob()
		{
			mProc = NULL;
			mParam = NULL;			
		}

		void Perform() override
		{
			mProc(mParam);
		}
	};

public:
	int mStackSize;	
	int mMaxThreads;
	CritSect mCritSect;
	SyncEvent mEvent;	
	Deque<Job*> mJobs;
	Array<Thread*> mThreads;
	int mFreeThreads;
	int mRunningThreads;
	bool mShuttingDown;

public:
	ThreadPool(int maxThreads = 4, int stackSize = 1024 * 1024);
	~ThreadPool();

	void Shutdown();
	void SetStackSize(int stackSize);
	void AddJob(Job* job, int maxWorkersPerProviderThread = 0x7FFFFFFF);
	void AddJob(BfpThreadStartProc proc, void* param, int maxWorkersPerProviderThread = 0x7FFFFFFF);
	bool IsInJob();
};

NS_BF_END