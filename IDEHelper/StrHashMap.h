#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_DBG_BEGIN

template <typename T>
class StrHashMap
{
public:
	struct Entry
	{
		T mValue;
		Entry* mNext;
		int mHash;
	};

	struct Iterator
	{
	public:
		StrHashMap* mMap;
		Entry* mCurEntry;
		int mCurBucket;

	public:
		Iterator(StrHashMap* map)
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

			if (mMap->mHashHeads == NULL)
				return *this; // At end

			while (mCurBucket < mMap->mHashSize)
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
	int GetHash(const char* str)
	{
		int curHash = 0;
		const char* curHashPtr = str;
		while (*curHashPtr != 0)
		{
			char c = *curHashPtr;
			if (c != ' ')
				curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
			curHashPtr++;
		}
		return curHash & 0x7FFFFFFF;
	}

	bool StrEqual(const char* strA, const char* strB)
	{
		const char* ptrA = strA;
		const char* ptrB = strB;

		while (true)
		{
			char ca;
			do { ca = *(strA++); } while (ca == ' ');
			char cb;
			do { cb = *(strB++); } while (cb == ' ');
			if (ca != cb)
				return false;
			if (ca == '\0')
				return true;
		}
	}

public:
	Beefy::BumpAllocator mAlloc;
	Entry** mHashHeads;
	int mHashSize;
	int mCount;

public:
	StrHashMap()
	{
		mHashHeads = NULL;
		mHashSize = 9973;
		mCount = 0;
	}

	void Insert(T value)
	{
		if (mHashHeads == NULL)
			mHashHeads = (Entry**)mAlloc.AllocBytes(sizeof(Entry*) * mHashSize, alignof(Entry*));

		int hash = GetHash(value->mName);
		int hashIdx = hash % mHashSize;
		Entry* headEntry = mHashHeads[hashIdx];

		Entry* newEntry = mAlloc.Alloc<Entry>();
		newEntry->mValue = value;
		newEntry->mNext = headEntry;
		newEntry->mHash = hash;
		mCount++;

		mHashHeads[hashIdx] = newEntry;
	}

	void Rehash(int newHashSize)
	{
		auto newHashHeads = (Entry**)mAlloc.AllocBytes(sizeof(Entry*) * newHashSize, alignof(Entry*));

		for (int hashIdx = 0; hashIdx < mHashSize; hashIdx++)
		{
			Entry* checkEntry = mHashHeads[hashIdx];
			while (checkEntry != NULL)
			{
				auto nextEntry = checkEntry->mNext;
				int newHashIdx = checkEntry->mHash % newHashSize;
				checkEntry->mNext = newHashHeads[newHashIdx];
				newHashHeads[newHashIdx] = checkEntry;

				checkEntry = nextEntry;
			}
		}

		mHashHeads = newHashHeads;
		mHashSize = newHashSize;
	}

	Entry* Find(const char* name)
	{
		if (mHashHeads == NULL)
			return NULL;

		// Make the lookup load reasonable
		if (mHashSize < mCount / 2)
		{
			Rehash(mCount / 2 + 123);
		}

		int hash = GetHash(name);
		int hashIdx = hash % mHashSize;
		Entry* checkEntry = mHashHeads[hashIdx];
		while (checkEntry != NULL)
		{
			if ((checkEntry->mHash == hash) && (StrEqual(name, checkEntry->mValue->mName)))
				return checkEntry;
			checkEntry = checkEntry->mNext;
		}
		return NULL;
	}

	void Clear()
	{
		mHashSize = 9973;
		mHashHeads = NULL;
		mAlloc.Clear();
		mCount = 0;
	}

	Iterator begin()
	{
		return ++Iterator(this);
	}

	Iterator end()
	{
		Iterator itr(this);
		itr.mCurBucket = mHashSize;
		return itr;
	}
};

NS_BF_DBG_END
