#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/Util/BumpAllocator.h"

NS_BF_DBG_BEGIN

template <typename T>
class StrBloomMap
{
public:
	static const int HashSize = 9973;
	Beefy::BumpAllocator mAlloc;

	struct Entry
	{		
		T mValue;
		Entry* mNext;
	};

	struct Iterator
	{
	public:
		StrBloomMap* mMap;
		Entry* mCurEntry;
		int mCurBucket;

	public:
		Iterator(StrBloomMap* map)
		{
			mMap = map;
			mCurBucket = 0;
			mCurEntry = NULL;	
		}

		Iterator& operator++()
		{
			if (mCurEntry != NULL)
			{
				mCurEntry = mCurEntry->mNext;
				if (mCurEntry != NULL)
					return *this;
				mCurBucket++;
			}

			while (mCurBucket < HashSize)
			{
				mCurEntry = mMap->mHashHeads[mCurBucket];
				if (mCurEntry != NULL)
					return *this;
				mCurBucket++;
			}

			return *this; // At end
		}

		bool operator!=(const Iterator& itr) const
		{
			return ((itr.mCurEntry != mCurEntry) || (itr.mCurBucket != mCurBucket));
		}

		Entry* operator*()
		{
			return mCurEntry;
		}
	};

public:
	int GetHash(const char* str, const char* strEnd)
	{	
		if (str == NULL)
			return 0;

		int curHash = 0;
		const char* curHashPtr = str;
		while ((*curHashPtr != 0) && (curHashPtr != strEnd))
		{
			char c = *curHashPtr;
			//if (c != ' ')
			curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
			curHashPtr++;
		}
		return curHash & 0x7FFFFFFF;
	}

public:	
	Entry** mHashHeads;

public:
	StrBloomMap()
	{
		mHashHeads = NULL;		
	}

	void InsertUnique(int hash, T value)
	{
		if (mHashHeads == NULL)
			mHashHeads = (Entry**)mAlloc.AllocBytes(sizeof(Entry*) * HashSize, alignof(Entry*));
		
		int hashIdx = hash % HashSize;
		Entry* headEntry = mHashHeads[hashIdx];

		Entry* checkEntry = headEntry;
		while (checkEntry != NULL)
		{
			if (checkEntry->mValue == value)
				return;
			checkEntry = checkEntry->mNext;
		}

		Entry* newEntry = mAlloc.Alloc<Entry>();
		newEntry->mValue = value;
		newEntry->mNext = headEntry;

		mHashHeads[hashIdx] = newEntry;
	}

	Entry* FindFirst(const char* name)
	{		
		if (mHashHeads == NULL)
			return NULL;

		int hash = GetHash(name, NULL) % HashSize;
		return mHashHeads[hash];		
	}

	Iterator begin()
	{
		return ++Iterator(this);
	}

	Iterator end()
	{
		Iterator itr(this);
		itr.mCurBucket = HashSize;
		return itr;
	}
};

NS_BF_DBG_END

