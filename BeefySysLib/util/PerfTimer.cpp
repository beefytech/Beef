#include "PerfTimer.h"
#include "BFApp.h"

#pragma warning(disable:4996)

#ifdef _DEBUG
#define PERF_WANTS_THREAD_CHECK
#endif

USING_NS_BF;

PerfManager* Beefy::gPerfManager = NULL;

PerfEntry::PerfEntry()
{
	mCount = 0;
	mTimeTotal = 0;
	mCurTimingStart = 0;	
	mChild = NULL;
}

void PerfEntry::DbgPrint(const StringImpl& name, int level, std::map<String, int>& uniqueTimingMap)
{	
	String text;

	uint64 childrenTime = 0;
	
	/*PerfEntryMap::iterator itr = mChildren.begin();
	while (itr != mChildren.end())
	{
		PerfEntry* child = &itr->second;
		childrenTime += child->mTimeTotal;
		++itr;
	}*/

	for (auto& kv : mChildren)
	{
		PerfEntry* child = kv.mValue;
		childrenTime += child->mTimeTotal;
	}

	for (int i = 0; i <= level; i++)
		text += "  ";
	String prefix = text;

	text += name;
	
	while (text.length() < 80)
		text += ' ';
	
	int selfTime = (int)(mTimeTotal - childrenTime);	

	if (uniqueTimingMap.find(name) == uniqueTimingMap.end())
		uniqueTimingMap[name] = selfTime;
	else
		uniqueTimingMap[name] += selfTime;

	text += StrFormat("%4d\t%6.1f\t%6.1f\n", mCount, mTimeTotal / 1000.0f, (int) (selfTime) / 1000.0f);

	/*for (int i = 0; i < (int) mLog.size(); i++)
	{
		PerfLogEntry* logEntry = &mLog[i];
		text += prefix + StrFormat("@%d: %s\n", logEntry->mTick, logEntry->mString.c_str());
	}*/
	
	OutputDebugStrF(text.c_str());

	for (auto& kv : mChildren)
	{
		PerfEntry* child = kv.mValue;
		child->DbgPrint(kv.mKey, level + 1, uniqueTimingMap);		
	}
}

void PerfManager::Clear()
{	
	mLogEntries.ClearFast();
	mAlloc.Clear();

	mFrames.clear();
	mCurStackIdx = 0;
	mOverhead = 0;
}

PerfManager::PerfManager()
{
	mRecording = false;
	mCurStackIdx = 0;
	mOwningThreadId = 0;
	mOverhead = 0;
	mAlloc.mDisableDebugTracing = true;
}

void PerfManager::NextFrame()
{
	if (!mRecording)
		return;

	BF_ASSERT(mCurStackIdx <= 1);
	while (mCurStackIdx > 0)
		ZoneEnd(true);

	auto logEntryFrameStart = mAlloc.Alloc<PerfLogEntry_FrameStart>();
	logEntryFrameStart->mType = PerfLogEntryType_FrameStart;
	logEntryFrameStart->mTimingStart = BFGetTickCountMicroFast();
	mLogEntries.PushBack(logEntryFrameStart);
	mCurStackIdx++;
}

void PerfManager::ZoneStart(const char* name)
{
	if (!mRecording)
		return;

#ifdef PERF_WANTS_THREAD_CHECK
    BF_ASSERT(mOwningThreadId == BfpThread_GetCurrentId());
#endif
	
	uint64 callTimerStart = BFGetTickCountMicroFast();

	mCurStackIdx++;
	int nameLen = (int)strlen(name);
	auto logEntryStart = mAlloc.Alloc<PerfLogEntry_Start>(nameLen + 1);
	char* namePtr = (char*)(logEntryStart + 1);
	strcpy(namePtr, name);
	logEntryStart->mType = PerfLogEntryType_Start;
	logEntryStart->mName = namePtr;	
	mLogEntries.PushBack(logEntryStart);	

	logEntryStart->mTimingStart = BFGetTickCountMicroFast();;
	mOverhead += logEntryStart->mTimingStart - callTimerStart;
	/*


	if (mCurStackIdx == 0)
		GetCurFrame();
	PerfEntry* curParent = mCurStack[mCurStackIdx - 1];

	PerfEntry* entry = NULL;	
	if (curParent->mChild != NULL)
	{
		if (curParent->mChild->mName == name)
			entry = curParent->mChild;
	}

	if (entry == NULL)
	{
		PerfEntryMap::iterator itr = curParent->mChildren.find(name);
	
		if (itr != curParent->mChildren.end())
		{
			entry = &itr->second;
		}
		else
		{
			itr = curParent->mChildren.insert(PerfEntryMap::value_type(name, PerfEntry())).first;
			entry = &itr->second;
			entry->mName = name;
		}

		curParent->mChild = entry;
	}

	entry->mCount++;
	entry->mCurTimingStart = BFGetTickCountMicroFast();
	mCurStack[mCurStackIdx++] = entry;

	mOverhead += entry->mCurTimingStart - callTimerStart;*/
}

void PerfManager::ZoneEnd(bool allowFrameEnd)
{	
	if (!mRecording)
		return;
	
	if ((mCurStackIdx <= 1)	&& (!allowFrameEnd))
		return;
	
	auto logEntryEnd = mAlloc.Alloc<PerfLogEntry_End>();	
	logEntryEnd->mType = PerfLogEntryType_End;
	logEntryEnd->mTimingEnd = BFGetTickCountMicroFast();
	mLogEntries.PushBack(logEntryEnd);
	mCurStackIdx--;

	/*PerfEntry* entry = mCurStack[mCurStackIdx - 1];
	mCurStackIdx--;
	entry->mTimeTotal += BFGetTickCountMicroFast() - entry->mCurTimingStart;*/
}

void PerfManager::Message(const StringImpl& theString)
{
	if (!mRecording)
		return;

	//TODO: Implement
	/*if (mCurStackIdx == 0)
		GetCurFrame();

	PerfEntry* entry = mCurStack[mCurStackIdx - 1];

	PerfLogEntry logEntry;
	logEntry.mTick = BFTickCount();
	logEntry.mString = theString;
	entry->mLog.push_back(logEntry);*/
}

void PerfManager::StartRecording()
{
#ifdef PERF_WANTS_THREAD_CHECK
    mOwningThreadId = BfpThread_GetCurrentId();
#endif

	mRecording = true;
	Clear();
	NextFrame();
}

void PerfManager::StopRecording(bool dbgPrint)
{
	if (!mRecording)
		return;

#ifdef PERF_WANTS_THREAD_CHECK
    BF_ASSERT(mOwningThreadId == ::BfpThread_GetCurrentId());
#endif	

	mRecording = false;
	
	auto logEntryStopRecording = mAlloc.Alloc<PerfLogEntry_StopRecording>();
	logEntryStopRecording->mType = PerfLogEntryType_StopRecording;
	logEntryStopRecording->mTimingEnd = BFGetTickCountMicroFast();
	mLogEntries.PushBack(logEntryStopRecording);

	if (dbgPrint)
		DbgPrint();

	/*if (mFrames.size() > 0)
	{
		PerfFrame* frame = &mFrames.back();
		frame->mTimeTotal = BFGetTickCountMicroFast() - frame->mCurTimingStart;
	}*/
}

bool PerfManager::IsRecording()
{
	return mRecording;
}

void PerfManager::DbgPrint()
{
#ifdef PERF_WANTS_THREAD_CHECK
    BF_ASSERT(mOwningThreadId == ::BfpThread_GetCurrentId());
#endif

	if (mFrames.empty())   
	{
		mCurStackIdx = 0;
		PerfFrame* curFrame = NULL;

		for (auto logEntry : mLogEntries)
		{
			if (logEntry->mType == PerfLogEntryType_FrameStart)
			{	
				PerfLogEntry_FrameStart* frameStartEntry = (PerfLogEntry_FrameStart*)logEntry;

				if (mFrames.size() > 0)
				{
					PerfFrame* frame = &mFrames.back();
					frame->mTimeTotal = frameStartEntry->mTimingStart - frame->mCurTimingStart;
				}

				mFrames.push_back(PerfFrame());
				PerfFrame* frame = &mFrames.back();
				frame->mCurTimingStart = frameStartEntry->mTimingStart;

#ifndef BF_NO_BFAPP
				if (gBFApp != NULL)
					frame->mAppFrameNum = gBFApp->mUpdateCnt;
				else
					frame->mAppFrameNum = 0;
#else
                frame->mAppFrameNum = 0; 
#endif

				frame->mCount = 1;

				mCurStack[mCurStackIdx++] = frame;
			}
			else if (logEntry->mType == PerfLogEntryType_Start)
			{
				PerfLogEntry_Start* entryStart = (PerfLogEntry_Start*)logEntry;

				PerfEntry* curParent = mCurStack[mCurStackIdx - 1];
				  
				PerfEntry* entry = NULL;
				if (curParent->mChild != NULL)
				{
					if (curParent->mChild->mName == entryStart->mName)
						entry = curParent->mChild;
				}

				if (entry == NULL)
				{
					/*PerfEntryMap::iterator itr = curParent->mChildren.find(entryStart->mName);

					if (itr != curParent->mChildren.end())
					{
						entry = &itr->second;
					}
					else
					{
						itr = curParent->mChildren.insert(PerfEntryMap::value_type(entryStart->mName, PerfEntry())).first;
						entry = &itr->second;
						entry->mName = entryStart->mName;
					}*/

					PerfEntry** entryPtr = NULL;
					if (curParent->mChildren.TryAdd(entryStart->mName, NULL, &entryPtr))
					{
						entry = mAlloc.Alloc<PerfEntry>();
						*entryPtr = entry;
						entry->mName = entryStart->mName;
					}
					else
					{
						entry = *entryPtr;
					}

					curParent->mChild = entry;
				}

				entry->mCount++;
				entry->mCurTimingStart = entryStart->mTimingStart;
				mCurStack[mCurStackIdx++] = entry;
			}
			else if (logEntry->mType == PerfLogEntryType_End)
			{
				PerfLogEntry_End* entryEnd = (PerfLogEntry_End*)logEntry;
				PerfEntry* entry = mCurStack[mCurStackIdx - 1];
				mCurStackIdx--;
				entry->mTimeTotal += entryEnd->mTimingEnd - entry->mCurTimingStart;				
			}
			else if (logEntry->mType == PerfLogEntryType_StopRecording)
			{
				PerfLogEntry_StopRecording* stopRecordingEntry = (PerfLogEntry_StopRecording*)logEntry;
				if (mFrames.size() > 0)
				{
					PerfFrame* frame = &mFrames.back();
					frame->mTimeTotal = stopRecordingEntry->mTimingEnd - frame->mCurTimingStart;					
				}
			}
		}
	}


	std::map<String, int> uniqueTimingMap;

	OutputDebugStrF("PerfManager_Results____________________________________________________________Calls___MSTotal__MSSelf\n");	                 
	PerfFrameList::iterator frameItr = mFrames.begin();
	while (frameItr != mFrames.end())
	{
		PerfFrame* frame = &(*frameItr);
		frame->DbgPrint(StrFormat("Frame %d", frame->mAppFrameNum), 0, uniqueTimingMap);
		++frameItr;
	}

	OutputDebugStr("\nPerfManager_Combined_Results____________________________________________________________________MSSelf\n");
	typedef std::multimap<int, String> SortedMap;
	SortedMap sortedMap;
	for (auto& entry : uniqueTimingMap)
	{
		sortedMap.insert(SortedMap::value_type(-entry.second, entry.first));
	}
	for (auto& entry : sortedMap)
	{
		String text = entry.second;
		for (int i = (int)text.length(); i < 96; i++)
			text += " ";
		text += StrFormat("%6.1f\n", -entry.first / 1000.0f);		
		OutputDebugStr(text.c_str());
	}

	{
		String text = "PerfTimer Timing Overhead";
		for (int i = (int) text.length(); i < 96; i++)
			text += " ";
		text += StrFormat("%6.1f\n", mOverhead / 1000.0f);
		OutputDebugStr(text.c_str());
	}		
}

AutoPerf::AutoPerf(const char* name, PerfManager* perfManager)
{
	if ((perfManager == NULL) || (!perfManager->mRecording))
	{
		mPerfManager = NULL;
		mStartStackIdx = 0;
		return;
	}
    mPerfManager = perfManager;
	mPerfManager->ZoneStart(name);	
	mStartStackIdx = mPerfManager->mCurStackIdx - 1;
}

void AutoPerf::Leave()
{
	if (mStartStackIdx <= 0)
		return;

	BF_ASSERT(mPerfManager->mCurStackIdx <= mStartStackIdx + 1);
	if (mPerfManager->mCurStackIdx > mStartStackIdx)
		mPerfManager->ZoneEnd();	

	mStartStackIdx = -1;
}

AutoPerf::~AutoPerf()
{
	Leave();	
}

//////////////////////////////////////////////////////////////////////////

AutoPerfRecordAndPrint::AutoPerfRecordAndPrint(PerfManager* perfManager)
{
	mPerfManager = perfManager;
	mDidStart = !perfManager->mRecording;
	if (mDidStart)
		perfManager->StartRecording();
}

AutoPerfRecordAndPrint::~AutoPerfRecordAndPrint()
{
	if (mDidStart)
		mPerfManager->StopRecording(true);
}

//////////////////////////////////////////////////////////////////////////

AutoDbgTime::AutoDbgTime(const StringImpl& name)
{
	mName = name;
	mStartTick = BFTickCount();
}

AutoDbgTime::~AutoDbgTime()
{
	int totalTime = BFTickCount() - mStartTick;
	OutputDebugStrF("%s: %d\n", mName.c_str(), totalTime);
}

///

#ifdef BFSYSLIB_DYNAMIC

BF_EXPORT void BF_CALLTYPE PerfTimer_ZoneStart(const char* name)
{
	gPerfManager->ZoneStart(name);
}

BF_EXPORT void BF_CALLTYPE PerfTimer_ZoneEnd()
{
	gPerfManager->ZoneEnd();
}

BF_EXPORT void BF_CALLTYPE PerfTimer_Message(const char* theString)
{
	gPerfManager->Message(theString);
}

BF_EXPORT int BF_CALLTYPE PerfTimer_IsRecording()
{
	return gPerfManager->IsRecording() ? 1 : 0;
}

BF_EXPORT void BF_CALLTYPE PerfTimer_StartRecording()
{
	gPerfManager->StartRecording();
}

BF_EXPORT void BF_CALLTYPE PerfTimer_StopRecording()
{
	gPerfManager->StopRecording();
}

BF_EXPORT void BF_CALLTYPE PerfTimer_DbgPrint()
{
	gPerfManager->DbgPrint();
}

#endif
