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
		Entry* mNext;
		int mHash;
	};

	struct Iterator
	{
	public:
		MultiHashSet* mSet;
		Entry* mCurEntry;
		int mCurBucket;

	public:
		Iterator(MultiHashSet* set)
		{
			this->mSet = set;
			this->mCurBucket = 0;
			this->mCurEntry = NULL;
		}

		Iterator& operator++()
		{
			if (this->mCurEntry != NULL)
			{
				this->mCurEntry = this->mCurEntry->mNext;
				if (this->mCurEntry != NULL)
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
				if (this->mCurEntry != NULL)
					return *this;
				this->mCurBucket++;
			}

			return *this; // At end
		}

		bool operator!=(const Iterator& itr) const
		{
			return ((itr.mCurEntry != this->mCurEntry) || (itr.mCurBucket != this->mCurBucket));
		}

		T operator*()
		{
			return this->mCurEntry->mValue;
		}

		operator bool() const
		{
			return this->mCurEntry != NULL;
		}

		void MoveToNextHashMatch()
		{
			int wantHash = this->mCurEntry->mHash;
			do 
			{
				this->mCurEntry = this->mCurEntry->mNext;
			} 
			while ((this->mCurEntry != NULL) && (this->mCurEntry->mHash != wantHash));
		}
	};

public:
	Entry** mHashHeads;
	static const int cDefaultHashSize = 17;
	int mHashSize;
	int mCount;
	
	MultiHashSet()
	{
		this->mHashHeads = NULL;
		this->mHashSize = cDefaultHashSize;
		this->mCount = 0;
	}

	~MultiHashSet()
	{
		this->Clear();
	}

	void Add(T value)
	{
		if (this->mHashHeads == NULL)
			this->mHashHeads = (Entry**)TFuncs::AllocateZero(sizeof(Entry*) * mHashSize, alignof(Entry*));

		int hash = TFuncs::GetHash(value);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		Entry* headEntry = this->mHashHeads[hashIdx];

		Entry* newEntry = (Entry*)TFuncs::Allocate(sizeof(Entry), alignof(Entry));
		newEntry->mValue = value;
		newEntry->mNext = headEntry;
		newEntry->mHash = hash;
		mCount++;

		mHashHeads[hashIdx] = newEntry;
	}

	void AddAfter(T value, Entry* afterEntry)
	{
		int hash = TFuncs::GetHash(value);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		BF_ASSERT(hash == afterEntry->mHash);
		
		Entry* newEntry = (Entry*)TFuncs::Allocate(sizeof(Entry), alignof(Entry));
		newEntry->mValue = value;
		newEntry->mNext = afterEntry->mNext;
		newEntry->mHash = hash;
		mCount++;

		afterEntry->mNext = newEntry;
	}

	void Rehash(int newHashSize)
	{
		auto newHashHeads = (Entry**)TFuncs::AllocateZero(sizeof(Entry*) * newHashSize, alignof(Entry*));

		SizedArray<Entry*, 32> entryList;

		for (int hashIdx = 0; hashIdx < mHashSize; hashIdx++)
		{
			Entry* checkEntry = mHashHeads[hashIdx];
			if (checkEntry != NULL)
			{
				// We want to keep elements with equal hashes in their insert order so we need to 
				// iterate through the linked list in reverse
				entryList.Clear();
				
				while (checkEntry != NULL)
				{
					entryList.Add(checkEntry);
					checkEntry = checkEntry->mNext;
				}
				
				for (int i = (int)entryList.mSize - 1; i >= 0; i--)
				{
					auto checkEntry = entryList[i];					
					int newHashIdx = (checkEntry->mHash & 0x7FFFFFFF) % newHashSize;
					checkEntry->mNext = newHashHeads[newHashIdx];
					newHashHeads[newHashIdx] = checkEntry;
				}
			}
		}

		TFuncs::Deallocate(mHashHeads);
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
		Entry* checkEntry = this->mHashHeads[hashIdx];
		while (checkEntry != NULL)
		{
			if ((checkEntry->mHash == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{
				if (val != NULL)
					*val = checkEntry->mValue;
				return true;
			}
			checkEntry = checkEntry->mNext;
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
		Entry* checkEntry = this->mHashHeads[hashIdx];
		while (checkEntry != NULL)
		{
			if ((checkEntry->mHash == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{
				Iterator itr(this);
				itr.mCurEntry = checkEntry;
				itr.mCurBucket = hashIdx;				
				return itr;
			}
			checkEntry = checkEntry->mNext;
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

		Entry** srcCheckEntryPtr = &this->mHashHeads[hashIdx];
		Entry* checkEntry = *srcCheckEntryPtr;
		while (checkEntry != NULL)
		{
			if ((checkEntry->mHash == hash) && (TFuncs::Matches(key, checkEntry->mValue)))
			{
				this->mCount--;
				*srcCheckEntryPtr = checkEntry->mNext;
				TFuncs::Deallocate(checkEntry);
				return true;
			}
			srcCheckEntryPtr = &checkEntry->mNext;
			checkEntry = checkEntry->mNext;
		}
		return false;
	}

	Iterator Erase(const Iterator& itr)
	{
		Iterator next = itr;
		++next;

		bool found = false;

		auto entry = itr.mCurEntry;		
		int hashIdx = (entry->mHash & 0x7FFFFFFF) % this->mHashSize;
		
		Entry** srcCheckEntryPtr = &this->mHashHeads[hashIdx];
		Entry* checkEntry = *srcCheckEntryPtr;
		while (checkEntry != NULL)
		{
			if (checkEntry == itr.mCurEntry)
			{
				this->mCount--;
				*srcCheckEntryPtr = checkEntry->mNext;
				found = true;
			}
			srcCheckEntryPtr = &checkEntry->mNext;
			checkEntry = checkEntry->mNext;
		}

		BF_ASSERT(found);
		TFuncs::Deallocate(entry);

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
				TFuncs::Deallocate(entry);
			}			
			TFuncs::Deallocate(this->mHashHeads);
		}
		
		this->mHashSize = cDefaultHashSize;
		this->mHashHeads = NULL;
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