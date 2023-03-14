#pragma once

#include "DebugCommon.h"
#include "WinDebugger.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_DBG_BEGIN

template <typename T>
class ProfileDataList
{
public:
	typedef T data_type;

public:
	T* mData;
	int mSize;

	bool operator==(const ProfileDataList& rhs)
	{
		if (mSize != rhs.mSize)
			return false;
		return memcmp(mData, rhs.mData, mSize * sizeof(T)) == 0;
	}
};

template <typename T>
struct ProfileDataListHash
{
	size_t operator()(const ProfileDataList<T>& val) const
	{
		return (size_t)Beefy::Hash64((void*)val.mData, val.mSize * sizeof(T));
	}
};

template <typename T>
struct ProfileDataListEquals
{
	bool operator()(const ProfileDataList<T>& lhs, const ProfileDataList<T>& rhs) const
	{
		if (lhs.mSize != rhs.mSize)
			return false;
		return memcmp(lhs.mData, rhs.mData, lhs.mSize * sizeof(T)) == 0;
	}
};

class ProfileAddrEntry : public ProfileDataList<addr_target>
{
public:
	//int mSampleCount;
	int mEntryIdx;

public:
	ProfileAddrEntry()
	{
		mEntryIdx = -1;
		//mSampleCount = 0;
	}
};

//typedef std::unordered_set<ProfileAddrEntry, ProfileDataListHash<addr_target>, ProfileDataListEquals<addr_target> > ProfileAddrEntrySet;
typedef HashSet<ProfileAddrEntry> ProfileAddrEntrySet;

class ProfileProcId
{
public:
	String mProcName;
	bool mIsIdle;
};

class ProfileProdIdEntry
{
public:
	ProfileProcId* mProcId;

	bool operator==(const ProfileProdIdEntry& rhs)
	{
		return rhs.mProcId->mProcName == mProcId->mProcName;
	}
};

class ProfileProcEntry : public ProfileDataList<ProfileProcId*>
{
public:
	int mSampleCount;
	int mEntryIdx;
	bool mUsed;

public:
	ProfileProcEntry()
	{
		mEntryIdx = -1;
		mSampleCount = 0;
		mUsed = false;
	}
};

//typedef std::unordered_set<ProfileProcEntry, ProfileDataListHash<ProfileProcId*>, ProfileDataListEquals<ProfileProcId*> > ProfileProcEntrySet;
typedef HashSet<ProfileProcEntry> ProfileProcEntrySet;

class ProfileThreadInfo
{
public:
	Array<int> mProfileAddrEntries;
	int mTotalSamples;
	int mTotalIdleSamples;
	String mName;

public:
	ProfileThreadInfo()
	{
		mTotalSamples = 0;
		mTotalIdleSamples = 0;
	}
};

class DbgProfiler : public Profiler
{
public:
	WinDebugger* mDebugger;
	volatile bool mIsRunning;
	bool mWantsClear;
	SyncEvent mShutdownEvent;
	bool mNeedsProcessing;
	uint32 mStartTick;
	uint32 mEndTick;

	int mTotalVirtualSamples;
	int mTotalActualSamples;
	int mTotalActiveSamplingMS;

	BumpAllocator mAlloc;
	Dictionary<uint, ProfileThreadInfo*> mThreadInfo;
	Array<uint> mThreadIdList;
	Dictionary<addr_target, bool> mStackHeadCheckMap;

	ProfileAddrEntrySet mProfileAddrEntrySet;
	Array<ProfileAddrEntry*> mProfileAddrEntries;
	Array<ProfileAddrEntry> mPendingProfileEntries;

	ProfileProcEntrySet mProfileProcEntrySet;
	Array<ProfileProcEntry*> mProfileProcEntries;

	Array<int> mProfileAddrToProcMap;

	Dictionary<void*, ProfileProcId*> mProcMap; // Keyed on either DwSubprogram or DwSymbol. Multiple pointers can reference the same ProfileProcId (in the case of inlined functions, for example)
	HashSet<ProfileProdIdEntry> mUniqueProcSet;
	HashSet<String> mIdleSymbolNames;

public:
	void ThreadProc();
	void AddEntries(String& str, Array<ProfileProcEntry*>& procEntries, int rangeStart, int rangeEnd, int stackIdx, ProfileProcId* findProc);

 	template <typename T>
 	typename T::key_type* AddToSet(T& map, typename T::key_type::data_type* data, int size)
 	{
 		typedef T::key_type::data_type DataType;

 		typename T::key_type entry;
 		entry.mData = data;
 		entry.mSize = size;

		typename T::key_type* entryPtr;
		if (map.TryAdd(entry, &entryPtr))
		{
			entryPtr->mData = (DataType*)mAlloc.AllocBytes(sizeof(DataType) * size);
			memcpy(entryPtr->mData, data, sizeof(DataType) * size);
		}

 		return entryPtr;
 	}

public:
	void HandlePendingEntries();
	void Process();
	void DoClear();
	ProfileProcId* Get(const StringImpl& str, bool* outIsNew = NULL);

public:
	DbgProfiler(WinDebugger* debugger);
	~DbgProfiler();

	void Start() override;
	void Stop() override;
	void Clear() override;

	bool IsSampling() override { return mIsRunning; }
	String GetOverview() override;
	String GetThreadList() override;
	String GetCallTree(int threadId, bool reverse) override;
};

NS_BF_DBG_END

namespace std
{
	template <>
	struct hash<NS_BF_DBG::ProfileProdIdEntry>
	{
		size_t operator()(const NS_BF_DBG::ProfileProdIdEntry& entry) const
		{
			return std::hash<Beefy::String>()(entry.mProcId->mProcName);
		}
	};

	template <>
	struct hash<NS_BF_DBG::ProfileProcEntry>
	{
		size_t operator()(const NS_BF_DBG::ProfileProcEntry& val) const
		{
			return (size_t)Beefy::Hash64((void*)val.mData, val.mSize * sizeof(NS_BF_DBG::ProfileProcId*));
		}
	};

	template <>
	struct hash<NS_BF_DBG::ProfileAddrEntry>
	{
		size_t operator()(const NS_BF_DBG::ProfileAddrEntry& val) const
		{
			return (size_t)Beefy::Hash64((void*)val.mData, val.mSize * sizeof(addr_target));
		}
	};

	template <typename T>
	struct hash<NS_BF_DBG::ProfileDataList<T> >
	{
		size_t operator()(const NS_BF_DBG::ProfileDataList<T>& val) const
		{
			return (size_t)Beefy::Hash64((void*)val.mData, val.mSize * sizeof(T));
		}
	};
}