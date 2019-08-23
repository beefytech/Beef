#pragma once

#include "../Common.h"

NS_BF_BEGIN;

class CritSect
{
public:
	BfpCritSect* mCritSect;	
	int32 mLockCount;

public:
	CritSect()
	{				
		mCritSect = BfpCritSect_Create();
		mLockCount = 0;
	}
		
	~CritSect()
	{
		BfpCritSect_Release(mCritSect);
		BF_ASSERT(mLockCount == 0);
	}	

	bool TryLock() 
	{ 
		bool locked = BfpCritSect_TryEnter(mCritSect, 0); 
		if (locked)
			mLockCount++;
		return locked;
	}	
	
	bool TryLock(int waitMS)
	{
		bool locked = BfpCritSect_TryEnter(mCritSect, waitMS);
		if (locked)
			mLockCount++;
		return locked;
	}

	void Lock() 
	{  
		BfpCritSect_Enter(mCritSect); 
		mLockCount++;
	}

	void Unlock() 
	{ 
		mLockCount--;
		BfpCritSect_Leave(mCritSect); 
	}
};

class SyncEvent
{
public:
	BfpEvent* mEvent;

public:
	SyncEvent(bool manualReset = false, bool initialState = false)
	{
		BfpEventFlags flags = (manualReset) ? BfpEventFlag_AllowManualReset : (BfpEventFlags)(BfpEventFlag_AllowAutoReset | BfpEventFlag_AllowManualReset);
		if (initialState)		
			flags = (BfpEventFlags)(flags | (manualReset ? BfpEventFlag_InitiallySet_Manual : BfpEventFlag_InitiallySet_Auto));
		mEvent = BfpEvent_Create(flags);
	}

	~SyncEvent()
	{
		BfpEvent_Release(mEvent);
	}
		
	void Set(bool requireManualReset = false)
	{
		BfpEvent_Set(mEvent, requireManualReset);		
	}
	
	void Reset()
	{
		BfpEvent_Reset(mEvent, NULL);
	}	

	bool WaitFor(int timeoutMS = -1)
	{
		return BfpEvent_WaitFor(mEvent, timeoutMS);
	}	
};

class AutoCrit
{
public:
	CritSect* mCritSect;
    
public:
	AutoCrit(CritSect& critSect)
	{
		mCritSect = &critSect;
		mCritSect->Lock();
	}
    
	~AutoCrit()
	{
		mCritSect->Unlock();
	}
};

NS_BF_END;
