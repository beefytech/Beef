#pragma once

#include "../Common.h"
#include <unordered_map>

#define NEW_DICTIONAY

NS_BF_BEGIN;

#ifdef NEW_DICTIONAY

template <typename TKey, typename TValue>
class Dictionary
{
public:
	typedef int int_cosize;
	typedef TKey key_type;
	typedef TValue value_type;	

	struct EntryPair
	{
	public:
		TKey mKey;           // Key of entry
		TValue mValue;         // Value of entry
	};

	struct RawEntryPair
	{
	public:
		typename std::aligned_storage<sizeof(TKey), alignof(TKey)>::type mKey;
		typename std::aligned_storage<sizeof(TValue), alignof(TValue)>::type mValue;
	};

public:	
	struct Entry : public RawEntryPair
	{
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
		//typedef T value_type;
		//typedef intptr difference_type;

		//typedef T* pointer;
		//typedef T& reference;

	public:
		Dictionary* mDictionary;
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

		iterator(Dictionary* dict, int idx)
		{
			mDictionary = dict;
			mIdx = idx;
			mCurrentIdx = idx;
		}		

		iterator(Dictionary* dict, int idx, int currentIdx)
		{
			mDictionary = dict;
			mIdx = idx;
			mCurrentIdx = currentIdx;
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

		bool NextWithSameKey(const TKey& key)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;

			while (mCurrentIdx >= 0)
			{	
				mCurrentIdx = mDictionary->mEntries[mCurrentIdx].mNext;
				if (mCurrentIdx < 0)
					break;

				if ((mDictionary->mEntries[mCurrentIdx].mHashCode == hashCode) &&
					((*(TKey*)&mDictionary->mEntries[mCurrentIdx].mKey) == key))
				{
					mIdx = mCurrentIdx;
					return true;
				}				
			}

			mIdx = mDictionary->mCount + 1;
			mCurrentIdx = -1;
			return false;
		}
		
		bool operator!=(const iterator& itr) const
		{
			return (itr.mDictionary != mDictionary) || (itr.mIdx != mIdx);
		}

		bool operator==(const iterator& itr) const
		{
			return (itr.mDictionary == mDictionary) && (itr.mIdx == mIdx);
		}

		EntryPair& operator*()
		{
			return *(EntryPair*)&mDictionary->mEntries[mCurrentIdx];
		}

		EntryPair* operator->()
		{
			return (EntryPair*)&mDictionary->mEntries[mCurrentIdx];
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
			new (&newEntry.mValue) TValue(std::move(*(TValue*)&oldEntry.mValue));			
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

	template <typename TAltKey>
	int FindEntryWith(const TAltKey& key)
	{
		if (mBuckets != NULL)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TAltKey>()(key) & 0x7FFFFFFF;
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

	bool Insert(const TKey& key, bool forceAdd, TKey** keyPtr, TValue** valuePtr)
	{		
		if (mBuckets == NULL) Initialize(0);
		int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;
		int_cosize targetBucket = hashCode % (int_cosize)mAllocSize;

		if (!forceAdd)
		{
			for (int_cosize i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
			{
				if ((mEntries[i].mHashCode == hashCode) && (*(TKey*)&mEntries[i].mKey == key))
				{
					if (keyPtr != NULL)
						*keyPtr = (TKey*)&mEntries[i].mKey;
					if (valuePtr != NULL)
						*valuePtr = (TValue*)&mEntries[i].mValue;
					return false;
				}
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
		
		if (keyPtr != NULL)
			*keyPtr = (TKey*)&mEntries[index].mKey;
		if (valuePtr != NULL)
			*valuePtr = (TValue*)&mEntries[index].mValue;
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
		((TValue*)&mEntries[i].mValue)->~TValue();
		mFreeList = i;
		mFreeCount++;
	}

public:
	Dictionary()
	{
		mBuckets = NULL;
		mEntries = NULL;
		mAllocSize = 0;
		mCount = 0;
		mFreeList = 0;
		mFreeCount = 0;
	}

	Dictionary(const Dictionary& val)
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
				new (&newEntry.mValue) TValue(*(TValue*)&oldEntry.mValue);
			}
			for (int i = mCount; i < mAllocSize; i++)
			{
				mEntries[i].mHashCode = -1;
			}
		}

		mFreeCount = val.mFreeCount;
		mFreeList = val.mFreeList;
	}

	Dictionary(Dictionary&& val)
	{
		mAllocSize = val.mAllocSize;
		mCount = val.mCount;
		mBuckets = val.mBuckets;
		mEntries = val.mEntries;
		mFreeCount = val.mFreeCount;
		mFreeList = val.mFreeList;
	}

	~Dictionary()
	{		
		DeleteData();
	}

	void AllocData(intptr size, Entry*& outEntries, int_cosize*& outBuckets)
	{
		uint8* data = new uint8[size * (sizeof(Entry) + sizeof(int_cosize))];
		outEntries = (Entry*)data;
		outBuckets = (int_cosize*)(data + size * sizeof(Entry));
	}

	void DeleteData()
	{
		if (!std::is_pod<TKey>::value)
		{
			for (int_cosize i = 0; i < mCount; i++)
			{
				if (mEntries[i].mHashCode != -1)
					((TKey*)&mEntries[i].mKey)->~TKey();
			}
		}

		if (!std::is_pod<TValue>::value)
		{
			for (int_cosize i = 0; i < mCount; i++)
			{
				if (mEntries[i].mHashCode != -1)
					((TValue*)&mEntries[i].mValue)->~TValue();
			}
		}

		delete [] mEntries;
	}

	Dictionary& operator=(const Dictionary& rhs)
	{
		Clear();
		for (auto& kv : rhs)
			ForceAdd(kv.mKey, kv.mValue);
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

	TValue& operator[](const TKey& key)
	{		
		TValue* valuePtr;
		if (Insert(key, false, NULL, &valuePtr))
		{
			new (valuePtr) TValue();
		}
		return *valuePtr;
	}

	const TValue& operator[](const TKey& key) const
	{
		int_cosize i = (int_cosize)FindEntry(key);
		if (i >= 0)		
			return mEntries[i].mValue;					
		BF_FATAL("Key not found");
		return TValue();
	}

	void ForceAdd(const TKey& key, const TValue& value)
	{
		TValue* valuePtr = NULL;
		Insert(key, true, NULL, &valuePtr);			
		new (valuePtr) TValue(value);
	}

	bool TryAdd(const TKey& key, const TValue& value)
	{
		TValue* valuePtr;
		if (!Insert(key, false, NULL, &valuePtr))
			return false;
		new (valuePtr) TValue(value);
		return true;
	}

	bool TryAdd(const TKey& key, TKey** keyPtr, TValue** valuePtr)
	{
		if (!Insert(key, false, keyPtr, valuePtr))
			return false;
		new (*valuePtr) TValue();
		return true;
	}

// 	template <typename TAltKey>
// 	bool TryAddWith(const TAltKey& key, TKey** keyPtr, TValue** valuePtr)
// 	{
// 		if (!Insert(key, false, keyPtr, valuePtr))
// 			return false;
// 		new (*valuePtr) TValue();
// 		return true;
// 	}

	// Returns uninitialized valuePtr - must use placement new
	bool TryAddRaw(const TKey& key, TKey** keyPtr, TValue** valuePtr)
	{
		return Insert(key, false, keyPtr, valuePtr);
	}

	bool TryGetValue(const TKey& key, TValue** valuePtr)
	{
		int_cosize i = (int_cosize)FindEntry(key);
		if (i >= 0)
		{
			*valuePtr = (TValue*)&mEntries[i].mValue;
			return true;
		}		
		return false;
	}

	template <typename TAltKey>
	bool TryGetValueWith(const TAltKey& key, TValue** valuePtr)
	{
		int_cosize i = (int_cosize)FindEntryWith(key);
		if (i >= 0)
		{
			*valuePtr = (TValue*)&mEntries[i].mValue;
			return true;
		}
		return false;
	}

	bool TryGetValue(const TKey& key, TValue* valuePtr)
	{
		int_cosize i = (int_cosize)FindEntry(key);
		if (i >= 0)
		{
			*valuePtr = *(TValue*)&mEntries[i].mValue;
			return true;
		}
		return false;
	}

	iterator Find(const TKey& key)
	{
		int_cosize i = (int_cosize)FindEntry(key);
		iterator itr;
		if (i >= 0)
		{
			return iterator((Dictionary*)this, i);
		}
		else
			return iterator((Dictionary*)this, mCount + 1, -1);
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

	bool Remove(const TKey& key, TValue* valuePtr)
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
					*valuePtr = *(TValue*)&mEntries[i].mValue;
					RemoveIdx(bucket, i, last);
					return true;
				}
			}
		}
		return false;
	}

	bool Remove(const TKey& key, const TValue& value)
	{
		if (mBuckets != NULL)
		{
			int_cosize hashCode = (int_cosize)BeefHash<TKey>()(key) & 0x7FFFFFFF;
			int_cosize bucket = hashCode % (int_cosize)mAllocSize;
			int_cosize last = -1;
			for (int_cosize i = mBuckets[bucket]; i >= 0; last = i, i = mEntries[i].mNext)
			{
				if ((mEntries[i].mHashCode == hashCode) && (*(TKey*)&mEntries[i].mKey == key) &&
					(*(TValue*)&mEntries[i].mValue == value))
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
			
			if (!std::is_pod<TValue>::value)
			{
				for (int_cosize i = 0; i < mCount; i++)
				{
					if (mEntries[i].mHashCode != -1)
						((TValue*)&mEntries[i].mValue)->~TValue();
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

	bool ContainsKey(const TKey& key)
	{
		return FindEntry(key) >= 0;
	}	
	
	iterator begin() const
	{
		iterator itr((Dictionary*)this, 0);
		++itr;
		return itr;
	}

	iterator end() const
	{
		return iterator((Dictionary*)this, mCount + 1, -1);
	}
};

#else

template <typename TKey, typename TValue>
class Dictionary : public std::unordered_map<TKey, TValue>
{
public:

public:
	TValue* GetValuePtr(const TKey& key)
	{
		auto itr = find(key);
		if (itr == end())
			return NULL;
		return &itr->second;
	}

	bool ContainsKey(const TKey& key)
	{
		return find(key) != end();
	}
};

#endif

NS_BF_END;
