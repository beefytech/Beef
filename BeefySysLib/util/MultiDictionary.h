#pragma once

#include "../Common.h"
#include "Array.h"

NS_BF_BEGIN;

struct MultiDictionaryFuncs : AllocatorCLib
{
	template <typename T>
	size_t GetHash(const T& value)
	{
		return BeefHash<T>()(value);
	}

	template <typename T>
	bool Matches(const T& lhs, const T& rhs)
	{
		return lhs == rhs;
	}
};

template <typename TKey, typename TValue, typename TFuncs = MultiDictionaryFuncs>
class MultiDictionary : public TFuncs
{
public:
	struct Entry
	{
		TKey mKey;
		TValue mValue;
		int mNext;
		int mHashCode;
	};

	struct EntryRef
	{
	public:
		MultiDictionary* mSet;
		int mIndex;

	public:
		EntryRef()
		{
			mSet = NULL;
			mIndex = -1;
		}

		EntryRef(MultiDictionary* set, int index)
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
		MultiDictionary* mDict;
		int mCurEntry;
		int mCurBucket;

	protected:
		Iterator()
		{
			this->mDict = NULL;
			this->mCurBucket = 0;
			this->mCurEntry = -1;
		}

	public:
		Iterator(MultiDictionary* dict)
		{
			this->mDict = dict;
			this->mCurBucket = 0;
			this->mCurEntry = -1;
		}

		Iterator& operator++()
		{
			if (this->mCurEntry != -1)
			{
				this->mCurEntry = this->mDict->mEntries[this->mCurEntry].mNext;
				if (this->mCurEntry != -1)
					return *this;
				this->mCurBucket++;
			}

			if (mDict->mHashHeads == NULL)
			{
				this->mCurBucket = this->mDict->mHashSize;
				return *this; // At end
			}

			while (this->mCurBucket < mDict->mHashSize)
			{
				this->mCurEntry = this->mDict->mHashHeads[mCurBucket];
				if (this->mCurEntry != -1)
					return *this;
				this->mCurBucket++;
			}

			return *this; // At end
		}

		TKey GetKey()
		{
			return this->mDict->mEntries[this->mCurEntry].mKey;
		}

		TValue GetValue()
		{
			return this->mDict->mEntries[this->mCurEntry].mValue;
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
			int wantHash = this->mDict->mEntries[this->mCurEntry].mHashCode;
			do
			{
				this->mCurEntry = this->mDict->mEntries[this->mCurEntry].mNext;
			} while ((this->mCurEntry != -1) && (this->mDict->mEntries[this->mCurEntry].mHashCode != wantHash));
			if (this->mCurEntry == -1)
				this->mCurBucket =  mDict->mHashSize;
		}
	};

	struct MatchIterator : public Iterator
	{
	public:
		TKey mKey;

	public:
		MatchIterator(const Iterator& iterator)
		{
			this->mDict = iterator.mDict;
			this->mCurBucket = iterator.mCurBucket;
			this->mCurEntry = iterator.mCurEntry;
		}

		MatchIterator(MultiDictionary* dict, const TKey& key) : Iterator(dict)
		{
			mKey = key;
		}

		MatchIterator& operator++()
		{
			while (true)
			{
				MoveToNextHashMatch();
				if (*this == mDict->end())
					break;
				if (mKey == GetKey())
					break;
			}			
			return *this;
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
		Entry* newEntries = TFuncs::allocate<Entry>(newSize);

		for (int i = 0; i < mCount; i++)
		{
			auto& newEntry = newEntries[i];
			auto& oldEntry = mEntries[i];
			newEntry.mHashCode = oldEntry.mHashCode;
			newEntry.mNext = oldEntry.mNext;
			new (&newEntry.mKey) TKey(std::move(*(TKey*)&oldEntry.mKey));
			new (&newEntry.mValue) TValue(std::move(*(TValue*)&oldEntry.mValue));
		}
		for (int i = mCount; i < newSize; i++)
		{
			newEntries[i].mHashCode = -1;
		}

		TFuncs::deallocate(mEntries);

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

	MultiDictionary()
	{
		this->mHashHeads = NULL;
		this->mHashSize = cDefaultHashSize;
		this->mEntries = NULL;
		this->mAllocSize = 0;
		this->mCount = 0;
		this->mFreeList = -1;
		this->mFreeCount = 0;
	}

	~MultiDictionary()
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
			this->mHashHeads = TFuncs::allocate<int>(mHashSize);
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

	void Add(TKey key, TValue value)
	{
		if (this->mHashHeads == NULL)
		{
			this->mHashHeads = TFuncs::allocate<int>(mHashSize);
			memset(this->mHashHeads, -1, sizeof(int) * mHashSize);
		}

		int index = AllocEntry();
		int hash = TFuncs::GetHash(key);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		int headEntry = this->mHashHeads[hashIdx];

		Entry* newEntry = &mEntries[index];
		newEntry->mKey = key;
		newEntry->mValue = value;
		newEntry->mNext = headEntry;
		newEntry->mHashCode = hash;

		mHashHeads[hashIdx] = index;
	}

	void AddAfter(TKey key, TValue value, Entry* afterEntry)
	{
		int hash = TFuncs::GetHash(key);
		int hashIdx = (hash & 0x7FFFFFFF) % this->mHashSize;
		BF_ASSERT(hash == afterEntry->mHashCode);

		int index = AllocEntry();
		Entry* newEntry = &mEntries[index];
		newEntry->mKey = key;
		newEntry->mValue = value;
		newEntry->mNext = afterEntry->mNext;
		newEntry->mHashCode = hash;

		afterEntry->mNext = index;
	}

	void Rehash(int newHashSize)
	{
		auto newHashHeads = TFuncs::allocate<int>(newHashSize);
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

			TFuncs::deallocate(mHashHeads);
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
	bool TryGet(const TKey& key, TKey* outKey, TValue* outValue)
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
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mKey)))
			{
				if (outKey != NULL)
					*outKey = checkEntry->mKey;
				if (outValue != NULL)
					*outValue = checkEntry->mValue;
				return true;
			}
			checkEntryIdx = checkEntry->mNext;
		}
		return false;
	}

	template <typename TKey>
	MatchIterator TryGet(const TKey& key)
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
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mKey)))
			{
				MatchIterator itr(this, key);
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
			if ((checkEntry->mHashCode == hash) && (TFuncs::Matches(key, checkEntry->mKey)))
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
		if (!TFuncs::deallocateAll())
		{
			auto itr = begin();
			auto endItr = end();
			while (itr != endItr)
			{
				auto entry = itr.mCurEntry;
				++itr;
				FreeIdx(entry);
			}
			TFuncs::deallocate(this->mHashHeads);
			TFuncs::deallocate(this->mEntries);
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