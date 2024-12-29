#pragma once

#include "Array.h"
#include <unordered_map>

NS_BF_BEGIN;

template <typename TKey, typename TAlloc = AllocatorCLib >
class HashSet : public TAlloc
{
public:
	typedef int int_cosize;
	typedef TKey key_type;	

public:
	struct Entry
	{
		typename std::aligned_storage<sizeof(TKey), alignof(TKey)>::type mKey;
		int_cosize mNext;        // Index of next entry, -1 if last
		int32 mHashCode;    // Lower 31 bits of hash code, -1 if unused		
	};

public:
	int_cosize* mBuckets;
	Entry* mEntries;
	int_cosize mAllocSize;
	int_cosize mCount;
	int_cosize mFreeList;
	int_cosize mFreeCount;

public:
	struct iterator
	{
	public:
		typedef std::bidirectional_iterator_tag iterator_category;
		
	public:
		HashSet* mDictionary;
		int_cosize mIdx;
		int_cosize mCurrentIdx;

	protected:
		void MoveNext()
		{
			while ((uintptr)mIdx < (uintptr)mDictionary->mCount)
			{
				if (mDictionary->mEntries[mIdx].mHashCode >= 0)
				{
					mCurrentIdx = mIdx;
					mIdx++;
					return;
				}
				mIdx++;
			}

			mIdx = mDictionary->mCount + 1;
			mCurrentIdx = -1;
		}

	public:
		iterator()
		{
			mDictionary = NULL;
			mIdx = 0;
			mCurrentIdx = -1;
		}

		iterator(HashSet* dict, int idx)
		{
			mDictionary = dict;
			mIdx = idx;
			mCurrentIdx = -1;
		}

		iterator& operator++()
		{
			MoveNext();
			return *this;
		}

		iterator operator++(int)
		{
			auto prevVal = *this;
			MoveNext();
			return prevVal;
		}

		bool operator!=(const iterator& itr) const
		{
			return (itr.mDictionary != mDictionary) || (itr.mIdx != mIdx);
		}

		bool operator==(const iterator& itr) const
		{
			return (itr.mDictionary == mDictionary) && (itr.mIdx == mIdx);
		}

		TKey& operator*()
		{
			return *(TKey*)&mDictionary->mEntries[mCurrentIdx].mKey;
		}

		TKey* operator->()
		{
			return (TKey*)&mDictionary->mEntries[mCurrentIdx].mKey;
		}
	};

	struct HashSelector
	{
		HashSet* mDictionary;
		int32 mHashCode;

		HashSelector(HashSet* dictionary, int32 hashCode)
		{
			mDictionary = dictionary;
			mHashCode = hashCode;
		}

		struct iterator
		{
		public:
			typedef std::bidirectional_iterator_tag iterator_category;

		public:
			HashSelector* mHashSelector;
			int_cosize mIdx;
			int_cosize mCurrentIdx;

		protected:
			void MoveNext()
			{
				auto dictionary = mHashSelector->mDictionary;

				while (mIdx >= 0)
				{
					if (dictionary->mEntries[mIdx].mHashCode == mHashSelector->mHashCode)
					{
						mCurrentIdx = mIdx;
						mIdx = dictionary->mEntries[mIdx].mNext;
						return;
					}
					else
					{
						mIdx = dictionary->mEntries[mIdx].mNext;
					}
				}

				mIdx = -1;
				mCurrentIdx = -1;
			}

		public:
			iterator()
			{
				mHashSelector = NULL;
				mIdx = 0;
				mCurrentIdx = -1;
			}

			iterator(HashSelector* hashSelector, int idx)
			{
				mHashSelector = hashSelector;
				mIdx = idx;
				mCurrentIdx = -1;
			}

			iterator& operator++()
			{
				MoveNext();
				return *this;
			}

			iterator operator++(int)
			{
				auto prevVal = *this;
				MoveNext();
				return prevVal;
			}

			/*iterator& operator--()
			{
			mPtr--;
			return *this;
			}

			iterator operator--(int)
			{
			auto prevVal = *this;
			mPtr--;
			return prevVal;
			}*/

			bool operator!=(const iterator& itr) const
			{
				return (itr.mHashSelector != mHashSelector) || (itr.mCurrentIdx != mCurrentIdx );
			}

			bool operator==(const iterator& itr) const
			{
				return (itr.mHashSelector == mHashSelector) && (itr.mCurrentIdx  == mCurrentIdx );
			}

			TKey& operator*()
			{
				return *(TKey*)&mHashSelector->mDictionary->mEntries[mCurrentIdx].mKey;
			}

			TKey* operator->()
			{
				return (TKey*)&mHashSelector->mDictionary->mEntries[mCurrentIdx].mKey;
			}
		};

		iterator begin()
		{
			int_cosize hashCode = (int_cosize)(mHashCode & 0x7FFFFFFF);

			iterator itr(this, mDictionary->mBuckets[hashCode % mDictionary->mAllocSize]);
			++itr;
			return itr;
		}

		iterator end()
		{
			return iterator(this, -1);
		}
	};

private:
	int_cosize GetPrimeish(int_cosize min)
	{
		// This is a minimal effort to help address-aligned dataa
		return (min | 1);
	}

	int_cosize ExpandSize(int_cosize oldSize)
	{
		int_cosize newSize = 2 * oldSize;

		// Allow the hashtables to grow to maximum possible size (~2G elements) before encoutering capacity overflow.
		// Note that this check works even when mAllocSize overflowed thanks to the (uint) cast
		/*if ((uint)newSize > MaxPrimeArrayLength && MaxPrimeArrayLength > oldSize)
		{
		Contract.Assert( MaxPrimeArrayLength == GetPrime(MaxPrimeArrayLength), "Invalid MaxPrimeArrayLength");
		return MaxPrimeArrayLength;
		}*/

		return GetPrimeish(newSize);
	}

	void Resize()
	{
		Resize(ExpandSize(mCount), false);
	}

	void Resize(intptr newSize, bool forceNewHashCodes)
	{
		BF_ASSERT(newSize >= mAllocSize);	
		Entry* newEntries;
		int_cosize* newBuckets;
		AllocData(newSize, newEntries, newBuckets);

		for (int_cosize i = 0; i < newSize; i++)
			newBuckets[i] = -1;
		
		for (int i = 0; i < mCount; i++)
		{
			auto& newEntry = newEntries[i];
			auto& oldEntry = mEntries[i];
			newEntry.mHashCode = oldEntry.mHashCode;
			newEntry.mNext = oldEntry.mNext;
			new (&newEntry.mKey) TKey(std::move(*(TKey*)&oldEntry.mKey));			
		}
		for (int i = mCount; i < newSize; i++)
		{
			newEntries[i].mHashCode = -1;
		}
		if (forceNewHashCodes)
		{
			for (int_cosize i = 0; i < mCount; i++)
			{
				if (newEntries[i].mHashCode != -1)
				{
					newEntries[i].mHashCode = (int_cosize)BeefHash<TKey>()(*(TKey*)&newEntries[i].mKey) & 0x7FFFFFFF;
				}
			}
		}
		for (int_cosize i = 0; i < mCount; i++)
		{
			if (newEntries[i].mHashCode >= 0)
			{
				int_cosize bucket = (int_cosize)(newEntries[i].mHashCode % newSize);
				newEntries[i].mNext = newBuckets[bucket];
				newBuckets[bucket] = i;
			}
		}

		DeleteData();		

		mBuckets = newBuckets;
		mEntries = newEntries;
		mAllocSize = (int_cosize)newSize;
	}

	int FindEntry(const TKey& key)
	{
		if (mBuckets != NULL)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;
			for (int_cosize i = mBuckets[hashCode % mAllocSize]; i >= 0; i = mEntries[i].mNext)
			{
				if (mEntries[i].mHashCode == hashCode && (*(TKey*)&mEntries[i].mKey == key)) return i;
			}
		}
		return -1;
	}

	template <typename TOther>
	int FindEntryWith(const TOther& key)
	{
		if (mBuckets != NULL)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TOther>()(key) & 0x7FFFFFFF;
			for (int_cosize i = mBuckets[hashCode % mAllocSize]; i >= 0; i = mEntries[i].mNext)
			{
				if (mEntries[i].mHashCode == hashCode && (*(TKey*)&mEntries[i].mKey == key)) return i;
			}
		}
		return -1;
	}

	void Initialize(intptr capacity)
	{
		int_cosize size = GetPrimeish((int_cosize)capacity);		
		AllocData(size, mEntries, mBuckets);

		mAllocSize = size;
		for (int_cosize i = 0; i < (int_cosize)mAllocSize; i++) mBuckets[i] = -1;		
		mFreeList = -1;
	}

	bool Insert(const TKey& key, bool add, TKey** keyPtr)
	{
		if (mBuckets == NULL) Initialize(0);
		int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;
		int_cosize targetBucket = hashCode % (int_cosize)mAllocSize;

		for (int_cosize i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
		{
 			if ((mEntries[i].mHashCode == hashCode) && (*(TKey*)&mEntries[i].mKey == key))
			{
				if (add)
				{
					BF_FATAL("Duplicate key");
					//ThrowUnimplemented();
					//ThrowHelper.ThrowArgumentException(ExceptionResource.Argument_AddingDuplicate);
				}
				//entries[i].value = value;
				//mVersion++;
				if (keyPtr != NULL)
					*keyPtr = (TKey*)&mEntries[i].mKey;				
				return false;
			}
		}
		int_cosize index;
		if (mFreeCount > 0)
		{
			index = mFreeList;
			mFreeList = mEntries[index].mNext;
			mFreeCount--;
		}
		else
		{
			if (mCount == mAllocSize)
			{
				Resize();
				targetBucket = hashCode % (int_cosize)mAllocSize;
			}
			index = mCount;
			mCount++;
		}

		mEntries[index].mHashCode = hashCode;
		mEntries[index].mNext = mBuckets[targetBucket];
		new (&mEntries[index].mKey) TKey(key);
		mBuckets[targetBucket] = index;
		//mVersion++;

		if (keyPtr != NULL)
			*keyPtr = (TKey*)&mEntries[index].mKey;		
		return true;
	}

	void RemoveIdx(int_cosize bucket, int_cosize i, int_cosize last)
	{
		if (last < 0)
		{
			mBuckets[bucket] = mEntries[i].mNext;
		}
		else
		{
			mEntries[last].mNext = mEntries[i].mNext;
		}
		mEntries[i].mHashCode = -1;
		mEntries[i].mNext = mFreeList;
		((TKey*)&mEntries[i].mKey)->~TKey();		
		mFreeList = i;
		mFreeCount++;
	}

public:
	HashSet()
	{
		mBuckets = NULL;
		mEntries = NULL;
		mAllocSize = 0;
		mCount = 0;
		mFreeList = 0;
		mFreeCount = 0;
	}

	HashSet(const HashSet& val)
	{
		mAllocSize = val.mAllocSize;
		mCount = val.mCount;
		
		if (mAllocSize == 0)
		{
			mBuckets = NULL;
			mEntries = NULL;
		}
		else
		{			
			AllocData(mAllocSize, mEntries, mBuckets);

			for (int_cosize i = 0; i < mAllocSize; i++)
				mBuckets[i] = val.mBuckets[i];

			for (int i = 0; i < mCount; i++)
			{
				auto& newEntry = mEntries[i];
				auto& oldEntry = val.mEntries[i];
				newEntry.mHashCode = oldEntry.mHashCode;
				newEntry.mNext = oldEntry.mNext;
				new (&newEntry.mKey) TKey(*(TKey*)&oldEntry.mKey);
			}
			for (int i = mCount; i < mAllocSize; i++)
			{
				mEntries[i].mHashCode = -1;
			}
		}
		
		mFreeCount = val.mFreeCount;
		mFreeList = val.mFreeList;
	}

	HashSet(HashSet&& val)
	{
		mAllocSize = val.mAllocSize;
		mCount = val.mCount;
		mBuckets = val.mBuckets;
		mEntries = val.mEntries;
		mFreeCount = val.mFreeCount;
		mFreeList = val.mFreeList;
	}

	void AllocData(intptr size, Entry*& outEntries, int_cosize*& outBuckets)
	{
		uint8* data = (uint8*)this->rawAllocate(size * (sizeof(Entry) + sizeof(int_cosize)));
		outEntries = (Entry*)data;
		outBuckets = (int_cosize*)(data + size * sizeof(Entry));
	}

	void DeleteData()
	{
		this->rawDeallocate(mEntries);		
	}

	~HashSet()
	{
		if (!std::is_pod<TKey>::value)
		{
			for (int_cosize i = 0; i < mCount; i++)
			{
				if (mEntries[i].mHashCode != -1)
					((TKey*)&mEntries[i].mKey)->~TKey();
			}
		}

		DeleteData();
	}

	HashSet& operator=(const HashSet& rhs)
	{
		Clear();
		for (auto& val : rhs)
			Add(val);
		return *this;
	}

	intptr GetCount() const
	{
		return mCount - mFreeCount;
	}

	intptr size() const
	{
		return mCount - mFreeCount;
	}

	bool IsEmpty() const
	{
		return mCount - mFreeCount == 0;
	}

	void Reserve(intptr size)
	{
		if (size > mAllocSize)
			Resize(size, false);
	}

	bool Add(const TKey& key)
	{		
		if (!Insert(key, false, NULL))
			return false;
		return true;
	}

	bool TryAdd(const TKey& key, TKey** keyPtr)
	{
		if (!Insert(key, false, keyPtr))
			return false;		
		return true;
	}

	bool TryGet(const TKey& key, TKey** keyPtr)
	{
		int idx = FindEntry(key);
		if (idx == -1)
			return false;
		*keyPtr = (TKey*)&mEntries[idx].mKey;
		return true;
	}

	template <typename TOther>
	bool TryGetWith(const TOther& key, TKey** keyPtr)
	{
		int idx = FindEntryWith(key);
		if (idx == -1)
			return false;
		*keyPtr = (TKey*)&mEntries[idx].mKey;
		return true;
	}

	bool Remove(const TKey& key)
	{
		if (mBuckets != NULL)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;
			int_cosize bucket = hashCode % (int_cosize)mAllocSize;
			int_cosize last = -1;
			for (int_cosize i = mBuckets[bucket]; i >= 0; last = i, i = mEntries[i].mNext)
			{
				if ((mEntries[i].mHashCode == hashCode) && (*(TKey*)&mEntries[i].mKey == key))
				{
					RemoveIdx(bucket, i, last);
					return true;
				}
			}
		}
		return false;
	}

	iterator Remove(const iterator& itr)
	{
		iterator nextItr = itr;
		++nextItr;

		auto& entry = mEntries[itr.mCurrentIdx];
		BF_ASSERT(entry.mHashCode >= 0);

		// We have to iterate through to mCurrentIdx so we can get 'last'
		int_cosize hashCode = entry.mHashCode;
		int_cosize bucket = hashCode % (int_cosize)mAllocSize;
		int_cosize last = -1;
		for (int_cosize i = mBuckets[bucket]; i >= 0; last = i, i = mEntries[i].mNext)
		{
			if ((mEntries[i].mHashCode == hashCode) && (i == itr.mCurrentIdx))
			{
				RemoveIdx(bucket, i, last);
				return nextItr;
			}
		}

		BF_FATAL("not found");
		return nextItr;
	}

	void Clear()
	{
		if (mCount > 0)
		{
			for (int_cosize i = 0; i < mAllocSize; i++) mBuckets[i] = -1;

			if (!std::is_pod<TKey>::value)
			{
				for (int_cosize i = 0; i < mCount; i++)
				{
					if (mEntries[i].mHashCode != -1)
						((TKey*)&mEntries[i].mKey)->~TKey();
				}
			}

			for (int_cosize i = 0; i < mCount; i++)
			{
				mEntries[i].mHashCode = -1;
			}

			mFreeList = -1;
			mCount = 0;
			mFreeCount = 0;
		}
	}

	bool Contains(const TKey& key)
	{
		return FindEntry(key) >= 0;
	}

	template <typename TOther>
	bool ContainsWith(const TOther& key)
	{
		return FindEntryWith(key) != -1;		
	}

	// Note: there's no const_iterator
	iterator begin() const
	{
		iterator itr((HashSet*)this, 0);
		++itr;
		return itr;
	}

	iterator end() const
	{
		return iterator((HashSet*)this, mCount + 1);
	}
	
	HashSelector SelectHashes(size_t hashCode)
	{
		return HashSelector(this, (int32)hashCode);
	}
};

NS_BF_END;
