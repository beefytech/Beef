#pragma once

#include "../Common.h"
//#include "boost/unordered/unordered_map.hpp"
#include "BumpAllocator.h"
#include "SLIList.h"
#include <unordered_map>
#include "BumpAllocator.h"
#include "Dictionary.h"
#include <map>
#include <list>

NS_BF_BEGIN;

class PerfEntry;

class PerfEntry
{
public:	
	String					mName;	
	Dictionary<String, PerfEntry*> mChildren;
	PerfEntry*				mChild;

	int						mCount;
	uint64					mTimeTotal;	
	uint64					mCurTimingStart;

public:
	PerfEntry();

	void					DbgPrint(const StringImpl& name, int level, std::map<String, int>& uniqueTimingMap);	
};

class PerfFrame : public PerfEntry
{
public:
	int						mAppFrameNum;
};

typedef std::list<PerfFrame> PerfFrameList;

const int MAX_PERFMANGER_STACKSIZE = 256;

enum PerfLogEntryType
{
	PerfLogEntryType_FrameStart,
	PerfLogEntryType_Start,
	PerfLogEntryType_End,
	PerfLogEntryType_Log,
	PerfLogEntryType_StopRecording
};

struct PerfLogEntry
{
	int mType;
	PerfLogEntry* mNext;
};

struct PerfLogEntry_FrameStart : PerfLogEntry
{	
	uint64 mTimingStart;
};

struct PerfLogEntry_StopRecording : PerfLogEntry
{
	uint64 mTimingEnd;
};

// We could use 32-bit timing start and timing end values and still 
//  record timings up to 71 minutes long
struct PerfLogEntry_Start : PerfLogEntry
{
	const char* mName;
	uint64 mTimingStart;
};

struct PerfLogEntry_End : PerfLogEntry
{	
	uint64 mTimingEnd;
};

class PerfManager
{
public:
	BumpAllocator			mAlloc;
	SLIList<PerfLogEntry*>	mLogEntries;

	PerfFrameList			mFrames;
	PerfEntry*				mCurStack[MAX_PERFMANGER_STACKSIZE];
	int						mCurStackIdx;
	bool					mRecording;	
    BfpThreadId				mOwningThreadId;
	uint64					mOverhead;	

protected:	
	void					Clear();	

public:
	PerfManager();

	void					NextFrame();
	
	void					ZoneStart(const char* name);
	void					ZoneEnd(bool allowFrameEnd = false);	
	void					Message(const StringImpl& theString);	
	
	void					StartRecording();
	void					StopRecording(bool dbgPrint = false);
	bool					IsRecording();
		
	void					DbgPrint();	
};

extern PerfManager* gPerfManager;

class AutoPerf
{
public:
    PerfManager* mPerfManager;
	int mStartStackIdx;
    
public:
	AutoPerf(const char* name, PerfManager* perfManager = gPerfManager);
	~AutoPerf();

	void Leave();
};

class AutoDbgTime
{
public:
	uint32 mStartTick;
	String mName;

public:
	AutoDbgTime(const StringImpl& name);
	~AutoDbgTime();
};

class AutoPerfRecordAndPrint
{
public:
	PerfManager* mPerfManager;
	bool mDidStart;

public:
	AutoPerfRecordAndPrint(PerfManager* perfManager = gPerfManager);
	~AutoPerfRecordAndPrint();
};


class DebugTimeGuard
{
public:
	uint32 mStartTick;
	int mMaxTicks;
	String mName;

public:
	DebugTimeGuard(int maxTicks, const StringImpl& name = "DebugTimeGuard")
	{
		mName = name;
		mMaxTicks = maxTicks;
		Start();
	}

	void Start()
	{
		mStartTick = BFTickCount();
	}

	void Stop()
	{
		if (mMaxTicks == 0)
			return;
		int maxTime = (int)(BFTickCount() - mStartTick);
		if (maxTime > mMaxTicks)
			OutputDebugStrF("%s took too long: %dms\n", mName.c_str(), maxTime);
		mMaxTicks = 0;
	}

	~DebugTimeGuard()
	{
		Stop();
	}
};

NS_BF_END;
