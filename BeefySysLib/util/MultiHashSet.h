#pragma once

#include "../Common.h"
#include "Array.h"

NS_BF_BEGIN;

struct MultiHashSetFuncs
{
	void* Allocate(intptr size, intptr align)
	{
		return malloc(size);
	}

	void* AllocateZero(intptr size, intptr align)
	{
		void* mem = malloc(size);
		memset(mem, 0, size);
		return mem;
	}

	void Deallocate(void* ptr)
	{
		free(ptr);
	}

	bool DeallocateAll()
	{
		return false;
	}
};

template <typename T, typename TFuncs = AllocatorCLib<T> >
class MultiHashSet : public TFuncs
{
public:
	struct Entry
	{
		T mValue;
		int mNext;
		int mHashCode;
	};

	struct EntryRef
	{
	public:
		MultiHashSet* mSet;
		int mIndex;

	public:
		EntryRef()
		{
			mSet = NULL;
			mIndex = -1;
		}

		EntryRef(MultiHashSet* set, int index)
		{
			mSet = set;
			mIndex = index;
		}

		Entry* operator*()
		{
			return &this->mSet->mEntries[this->mIndex];
		}

		Entry* operator->()
		{
			return &this->mSet->mEntries[this->mIndex];
		}

		operator bool() const
		{
			return this->mIndex != -1;
		}
	};

	struct Iterator
	{
	public:		
		MultiHashSet* mSet;
		int mCurEntry;
		int mCurBucket;

	public:
		Iterator(MultiHashSet* set)
		{
			this->mSet = set;
			this->mCurBucket = 0;
			this->mCurEntry = -1;
		}

		Iterator& operator++()
		{
			if (this->mCurEntry != -1)
			{
				this->mCurEntry = this->mSet->mEntries[this->mCurEntry].mNext;
				if (this->mCurEntry != -1)
					return *this;
				this->mCurBucket++;
			}

			if (mSet->mHashHeads == NULL)
			{
				this->mCurBucket = this->mSet->mHashSize;
				return *this; // At end
			}

			while (this->mCurBucket < mSet->mHashSize)
			{
				this->mCurEntry = this->mSet->mHashHeads[mCurBucket];
				if (this->mCurEntry != -1)
					return *this;
				this->mCurBucket++;
			}

			return *this; // At end
		}

		T operator*()
		{
			return this->mSet->mEntries[this->mCurEntry].mValue;
		}

		T operator->()
		{
			return this->mSet->mEntries[this->mCurEntry].mValue;
		}

		bool operator!=(const Iterator& itr) const
		{
			return ((itr.mCurEntry != this->mCurEntry) || (itr.mCurBucket != this->mCurBucket));
		}

		operator bool() const
		{
			return this->mCurEntry != -1;
		}
		
		void MoveToNextHashMatch()
		{
			int wantHash = this->mSet->mEntries[this->mCurEntry].mHashCode;
			do 
			{
				this->mCurEntry = this->mSet->mEntries[this->mCurEntry].mNext;
			} 
			while ((this->mCurEntry != -1) && (this->mSet->mEntries[this->mCurEntry].mHashCode != wantHash));
		}
	};

protected:

	int GetPrimeish(int min)
	{
		// This is a minimal effort to help address-aligned dataa
		return (min | 1);
	}

	int ExpandSize(int oldSize)
	{
		int newSize = 2 * oldSize;

		// Allow the hashtables to grow to maximum possible size (~2G elements) before encoutering capacity overflow.
		// Note that this check works even when mAllocSize overflowed thanks to the (uint) cast
		/*if ((uint)newSize > MaxPrimeArrayLength && MaxPrimeArrayLength > oldSize)
		{
		Contract.Assert( MaxPrimeArrayLength == GetPrime(MaxPrimeArrayLength), "Invalid MaxPrimeArrayLength");
		return MaxPrimeArrayLength;
		}*/

		return GetPrimeish(newSize);
	}

	void ResizeEntries()
	{
		ResizeEntries(ExpandSize(mCount));
	}

	void ResizeEntries(int newSize)
	{
		BF_ASSERT(newSize >= mAllocSize);
		Entry* newEntries = (Entry*)TFuncs::Allocate(sizeof(Entry) * newSize, alignof(Entry));
		
		for (int i = 0; i < mCount; i++)
		{
			auto& newEntry = newEntries[i];
			auto& oldEntry = mEntries[i];
			newEntry.mHashCode = oldEntry.mHashCode;
			newEntry.mNext = oldEntry.mNext;
			new (&newEntry.mValue) T(std::move(*(T*)&oldEntry.mValue));
		}
		for (int i = mCount; i < newSize; i++)
		{
			newEntries[i].mHashCode = -1;
		}
		
		TFuncs::Deallocate(mEntries);
		
		mEntries = newEntries;
		mAllocSize = (int)newSize;
	}

	void FreeIdx(int entryIdx)
	{
		this->mEntries[entryIdx].mNext = this->mFreeList;
		this->mFreeList = entryIdx;
		this->mFreeCount++;
	}

	int AllocEntry()
	{
		int index;
		if (this->mFreeCount > 0)
		{
			index = this->mFreeList;
			this->mFreeList = this->mEntries[index].mNext;
			this->mFreeCount--;
		}
		else
		{
			if (this->mCount == this->mAllocSize)
				ResizeEntries();
			index = mCount;
			this->mCount++;
		}
		return index;
	}

public:	
	int* mHashHeads;
	int mAllocSize;
	Entry* mEntries;
	int mFreeList;
	int mFreeCount;
	
	static const int cDefaultHashSize = 17;
	int mHashSize;
	int mCount;
	
	MultiHashSet()
	{		
		this->mHashHeads = NULL;
		this->mHashSize = cDefaultHashSize;
		this->mEntries = NULL;
		this->mAllocSize = 0;
		this->mCount = 0;
		this->mFreeList = -1;
		this->mFreeCount = 0;
	}

	~MultiHashSet()
	{
		this->Clear();
	}

	void EnsureFreeCount(int wantFreeCount)
	{
		int freeCount = mFreeCount + (mAllocSize - mCount);
		if (freeCount >= wantFreeCount)
			return;
		ResizeEntries(BF_MAX(ExpandSize(mCount), mAllocSize + wantFreeCount - freeCount));
	}

	int GetCount() const
	{
		return mCount - mFreeCount;
	}

	int size() const
	{
		return mCount - mFreeCount;
	}

	EntryRef AddRaw(int hash)
	{
		if (this->mHashHeads == NULL)
		{
			this->mHashHeads = (int*)TFuncs::Allocate(sizeof(int) * mHashSize, alignof(int));
			memset(this->mHashHeads, -1, sizeof(int) * mHashSize);
		}

		int index = AllocEntry();		
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		int headEntry = this->mHashHeads[hashIdx];

		Entry* newEntry = &mEntries[index];
		newEntry->mValue = T();
		newEntry->mNext = headEntry;
		newEntry->mHashCode = hash;

		mHashHeads[hashIdx] = index;

		return EntryRef(this, index);
	}

	void Add(T value)
	{
		if (this->mHashHeads == NULL)
		{			
			this->mHashHeads = (int*)TFuncs::Allocate(sizeof(int) * mHashSize, alignof(int));
			memset(this->mHashHeads, -1, sizeof(int) * mHashSize);
		}

		int index = AllocEntry();
		int hash = TFuncs::GetHash(value);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		int headEntry = this->mHashHeads[hashIdx];

		Entry* newEntry = &mEntries[index];
		newEntry->mValue = value;
		newEntry->mNext = headEntry;
		newEntry->mHashCode = hash;		

		mHashHeads[hashIdx] = index;		
	}

	void AddAfter(T value, Entry* afterEntry)
	{
		int hash = TFuncs::GetHash(value);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		BF_ASSERT(hash == afterEntry->mHashCode);
		
		int index = AllocEntry();
		Entry* newEntry = &mEntries[index];
		newEntry->mValue = value;
		newEntry->mNext = afterEntry->mNext;
		newEntry->mHashCode = hash;		

		afterEntry->mNext = index;
	}

	void Rehash(int newHashSize)
	{
		auto newHashHeads = (int*)TFuncs::Allocate(sizeof(int) * newHashSize, alignof(int));
		memset(newHashHeads, -1, sizeof(int) * newHashSize);
		
		if (mHashHeads != NULL)
		{
			SizedArray<int, 1024> entryList;
			for (int hashIdx = 0; hashIdx < mHashSize; hashIdx++)
			{
				int checkEntryIdx = mHashHeads[hashIdx];
				if (checkEntryIdx != -1)
				{
					// We want to keep elements with equal hashes in their insert order so we need to 
					// iterate through the linked list in reverse
					entryList.Clear();

					while (checkEntryIdx != -1)
					{
						entryList.Add(checkEntryIdx);
						checkEntryIdx = mEntries[checkEntryIdx].mNext;
					}

					for (int i = (int)entryList.mSize - 1; i >= 0; i--)
					{
						int checkEntryIdx = entryList[i];
						auto checkEntry = &mEntries[checkEntryIdx];
						int newHashIdx = (checkEntry->mHashCode & 0x7FFFFFFF) % newHashSize;
						checkEntry->mNext = newHashHeads[newHashIdx];
						newHashHeads[newHashIdx] = checkEntryIdx;
					}
				}
			}

			TFuncs::Deallocate(mHashHeads);
		}
		mHashHeads = newHashHeads;
		mHashSize = newHashSize;
	}

	void CheckRehash()
	{
		// Make the lookup load reasonable
		if (mHashSize < mCount)
		{
			this->Rehash(BF_MAX(mCount, (int)(mHashSize * 1.5f)) | 1);
		}
	}

	template <typename TKey>
	bool TryGet(const TKey& key, T* val)
	{
		if (mHashHeads == NULL)
			return false;

		this->CheckRehash();

		int hash = TFuncs::GetHash(key);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		int checkEntryIdx = this->mHashHeads[hashIdx];
		while (checkEntryIdx != -1)
		{
			Entry* checkEntry = &mEntries[checkEntryIdx];
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{
				if (val != NULL)
					*val = checkEntry->mValue;
				return true;
			}
			checkEntryIdx = checkEntry->mNext;
		}
		return false;
	}

	template <typename TKey>
	Iterator TryGet(const TKey& key)
	{
		if (mHashHeads == NULL)
			return end();

		this->CheckRehash();

		int hash = TFuncs::GetHash(key);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		int checkEntryIdx = this->mHashHeads[hashIdx];		
		while (checkEntryIdx != -1)
		{
			auto checkEntry = &this->mEntries[checkEntryIdx];
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{
				Iterator itr(this);
				itr.mCurEntry = checkEntryIdx;
				itr.mCurBucket = hashIdx;				
				return itr;
			}
			checkEntryIdx = checkEntry->mNext;			
		}

		return end();
	}

	template <typename TKey>
	bool Remove(const TKey& key)
	{
		if (mHashHeads == NULL)
			return false;

		this->CheckRehash();

		int hash = TFuncs::GetHash(key);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;

		int* srcCheckEntryPtr = &this->mHashHeads[hashIdx];
		int checkEntryIdx = *srcCheckEntryPtr;
		while (checkEntryIdx != -1)
		{
			auto checkEntry = &mEntries[checkEntryIdx];
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{				
				*srcCheckEntryPtr = checkEntry->mNext;
				FreeIdx(checkEntryIdx);
				return true;
			}
			srcCheckEntryPtr = &checkEntry->mNext;
			checkEntryIdx = checkEntry->mNext;
		}
		return false;
	}

	Iterator Erase(const Iterator& itr)
	{
		Iterator next = itr;
		++next;

		bool found = false;

		auto entryIdx = itr.mCurEntry;		
		auto entry = &mEntries[entryIdx];
		int hashIdx = (entry->mHashCode & 0x7FFFFFFF) % this->mHashSize;
		
		int* srcCheckEntryPtr = &this->mHashHeads[hashIdx];
		int checkEntryIdx = *srcCheckEntryPtr;
		while (checkEntryIdx != -1)
		{
			auto checkEntry = &mEntries[checkEntryIdx];
			if (checkEntryIdx == itr.mCurEntry)
			{
				*srcCheckEntryPtr = checkEntry->mNext;
				found = true;
			}
			srcCheckEntryPtr = &checkEntry->mNext;
			checkEntryIdx = checkEntry->mNext;
		}

		BF_ASSERT(found);
		FreeIdx(entryIdx);

		return next;
	}
	
	void Clear()
	{
		if (!TFuncs::DeallocateAll())
		{	
			auto itr = begin();
			auto endItr = end();
			while (itr != endItr)
			{
				auto entry = itr.mCurEntry;
				++itr;
				FreeIdx(entry);
			}			
			TFuncs::Deallocate(this->mHashHeads);
			TFuncs::Deallocate(this->mEntries);
		}
		
		this->mHashSize = cDefaultHashSize;
		this->mHashHeads = NULL;
		this->mEntries = NULL;
		this->mCount = 0;
	}

	Iterator begin()
	{
		return ++Iterator(this);
	}

	Iterator end()
	{
		Iterator itr(this);
		itr.mCurBucket = this->mHashSize;
		return itr;
	}
};

NS_BF_END;