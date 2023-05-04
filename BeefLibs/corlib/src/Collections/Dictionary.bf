// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_DICTIONARY
#endif

using System;
using System.Collections;
using System.Diagnostics;
using System.Diagnostics.Contracts;
using System.Threading;

namespace System.Collections
{
	interface IDictionary
	{
		Variant this[Variant key]
		{
			get;
			set;
		}

		bool ContainsKey(Variant key);
		bool ContainsValue(Variant value);
		void Add(Variant key, Variant value);
		void Clear();
		void Remove(Variant key);
	}

	public class Dictionary<TKey, TValue> :
		IDictionary,
		ICollection<(TKey key, TValue value)>,
		IEnumerable<(TKey key, TValue value)>,
		IRefEnumerable<(TKey key, TValue* valueRef)> where TKey : IHashable
	{
		typealias KeyValuePair=(TKey key, TValue value);
		typealias KeyRefValuePair=(TKey key, TValue* valueRef);

		private struct Entry			
		{
			public TKey mKey;           // Key of entry
			public TValue mValue;         // Value of entry
			public int_cosize mHashCode;    // some bits of hash code, -1 if unused
			public int_cosize mNext;        // Index of next entry, -1 if last
		}

		int_cosize* mBuckets;
		Entry* mEntries;
		int_cosize mAllocSize;
		int_cosize mCount;
		int_cosize  mFreeList;
		int_cosize mFreeCount;
#if VERSION_DICTIONARY
		private int32 mVersion;
		const String cVersionError = "Dictionary changed during enumeration";
#endif
		
		public this(): this(0) { }

		public this(int_cosize capacity)
		{
			//if (capacity < 0) ThrowHelper.ThrowArgumentOutOfRangeException(ExceptionArgument.capacity);
			if (capacity > 0) Initialize(capacity);
			//TODO: this.comparer = comparer ?? EqualityComparer<TKey>.Default;
        	}
	
		public this(IEnumerator<KeyValuePair> enumerator)
		{
			for (var kv in enumerator)
				this[kv.key] = kv.value;
		}

		public ~this()
		{
			if (mEntries != null)
			{
				var entries = mEntries;
#if BF_ENABLE_REALTIME_LEAK_CHECK
				// To avoid scanning items being deleted
				mEntries = null;
				Interlocked.Fence();
#endif
				Free(entries);
			}
		}

		public int Count
		{
			get { return mCount - mFreeCount; }
		}

		public bool IsEmpty
		{
			get { return mCount - mFreeCount == 0; }
		}

		public ValueEnumerator Values
		{
			get
			{
				//Contract.Ensures(Contract.Result<ValueCollection>() != null);
				return ValueEnumerator(this);
			}
		}

		public KeyEnumerator Keys
		{
			get
			{
				//Contract.Ensures(Contract.Result<ValueCollection>() != null);
				return KeyEnumerator(this);
			}
		}

		/*ICollection<TValue> IDictionary<TKey, TValue>.Values
		{
			get
			{
				if (values == null) values = new ValueCollection(this);
				return values;
			}
		}

		IEnumerable<TValue> IReadOnlyDictionary<TKey, TValue>.Values
		{
			get
			{
				if (values == null) values = new ValueCollection(this);
				return values;
			}
		}*/

		public ref TValue this[TKey key]
		{
			get
			{
				int_cosize i = (int_cosize)FindEntry(key);
				if (i >= 0) return ref mEntries[i].mValue;
				Runtime.FatalError("Key not found");
			}
			set
			{
				Insert(key, value, false);
			}
		}

		
		Variant IDictionary.this[Variant key]
		{
			get
			{
				return [Unbound]Variant.Create(this[key.Get<TKey>()]);
			}
			set
			{
				this[key.Get<TKey>()] = value.Get<TValue>();
			}
		}

		public void Add(TKey key, TValue value)
		{
			Insert(key, value, true);
		}

		void IDictionary.Add(Variant key, Variant value)
		{
			Add(key.Get<TKey>(), value.Get<TValue>());
		}

		public void Add(KeyValuePair kvPair)
		{
			Insert(kvPair.key, kvPair.value, true);
		}

		public bool TryAdd(TKey key, TValue value)
		{
			TKey* keyPtr;
			TValue* valuePtr;
			bool inserted = Insert(key, false, out keyPtr, out valuePtr, null);
			if (!inserted)
				return false;
			*keyPtr = key;
			*valuePtr = value;
			return true;
		}

		public bool TryAdd(TKey key, out TKey* keyPtr, out TValue* valuePtr)
		{
            return Insert(key, false, out keyPtr, out valuePtr, null);
		}

		public bool TryAddAlt<TAltKey>(TAltKey key, out TKey* keyPtr, out TValue* valuePtr) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
		    return InsertAlt(key, false, out keyPtr, out valuePtr, null);
		}

		protected virtual (Entry*, int_cosize*) Alloc(int size)
		{
			int byteSize = size * (strideof(Entry) + sizeof(int_cosize));
			uint8* allocPtr = new uint8[byteSize]*;
			return ((Entry*)allocPtr, (int_cosize*)(allocPtr + size * strideof(Entry)));
		}

		protected virtual void Free(Entry* entryPtr)
		{
			delete (void*)entryPtr;
		}

		protected override void GCMarkMembers()
		{
			if (mEntries == null)
				return;
			let type = typeof(Entry);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
		    for (int i < mCount)
		    {
		        mEntries[i].[Friend]GCMarkMembers();
			}
		}

		public enum AddResult
		{
			case Added(TKey* keyPtr, TValue* valuePtr);
			case Exists(TKey* keyPtr, TValue* valuePtr);
		}

		public AddResult TryAdd(TKey key)
		{
			TKey* keyPtr;
			TValue* valuePtr;			
			if (Insert(key, false, out keyPtr, out valuePtr, null))
				return .Added(keyPtr, valuePtr);
			return .Exists(keyPtr, valuePtr);
		}

		/*void ICollection<KeyValuePair<TKey, TValue>>.Add(KeyValuePair<TKey, TValue> keyValuePair)
		{
			Add(keyValuePair.Key, keyValuePair.Value);
		}

		bool ICollection<KeyValuePair<TKey, TValue>>.Contains(KeyValuePair<TKey, TValue> keyValuePair)
		{
			int i = FindEntry(keyValuePair.Key);
			if (i >= 0 && EqualityComparer<TValue>.Default.Equals(entries[i].value, keyValuePair.Value))
			{
				return true;
			}
			return false;
		}

		bool ICollection<KeyValuePair<TKey, TValue>>.Remove(KeyValuePair<TKey, TValue> keyValuePair)
		{
			int i = FindEntry(keyValuePair.Key);
			if (i >= 0 && EqualityComparer<TValue>.Default.Equals(entries[i].value, keyValuePair.Value))
			{
				Remove(keyValuePair.Key);
				return true;
			}
			return false;
		}*/

		public Result<TValue> GetValue(TKey key)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0) return mEntries[i].mValue;
			return .Err;
		}

		public void Clear()
		{
			if (mCount > 0)
			{
				for (int_cosize i = 0; i < mAllocSize; i++) mBuckets[i] = -1;
				//for (int_cosize i = 0; i < mCount; i++)
					//mEntries[i] = default(Entry);
				//Array.Clear(entries, 0, count);
				mFreeList = -1;
				mCount = 0;
				mFreeCount = 0;
#if VERSION_DICTIONARY
				mVersion++;
#endif
			}
		}

		public bool ContainsKey(TKey key)
		{
			return FindEntry(key) >= 0;
		}

		bool IDictionary.ContainsKey(Variant key)
		{
			return ContainsKey(key.Get<TKey>());
		}

		public bool ContainsKeyAlt<TAltKey>(TAltKey key) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			return FindEntryAlt(key) >= 0;
		}

		public bool ContainsValue(TValue value)
		{
			for (int_cosize i = 0; i < mCount; i++)
			{
				if (mEntries[i].mHashCode >= 0 && mEntries[i].mValue == value) return true;
			}
			return false;
		}

		bool IDictionary.ContainsValue(Variant value)
		{
			return ContainsValue(value.Get<TValue>());
		}
		
		public bool Contains(KeyValuePair kvPair)
		{
			TValue value;
			if (TryGetValue(kvPair.key, out value))
			{
				return value == kvPair.value;
			}
			else
			{
				return false;
			}
		}

		public bool ContainsAlt<TAltKey>((TAltKey key, TValue value) kvPair) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			TValue value;
			if (TryGetValueAlt(kvPair.key, out value))
			{
				return value == kvPair.value;
			}
			else
			{
				return false;
			}
		}
		
		public void CopyTo(Span<KeyValuePair> kvPair)
		{
			Debug.Assert(kvPair.Length >= Count);
			int idx = 0;
			for (var kv in this)
			{
				kvPair[idx] = kv;
				++idx;
			}
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this, Enumerator.[Friend]KeyValuePair);
		}

		static int_cosize GetKeyHash(int hashCode)
		{
			if (sizeof(int) == 4)
				return (int32)hashCode & 0x7FFFFFFF;
#unwarn
			if (sizeof(int_cosize) == 8)
				return (int_cosize)(hashCode & 0x7FFFFFFF'FFFFFFFFL);
			return ((int32)hashCode ^ (int32)((int64)hashCode >> 33)) & 0x7FFFFFFF;
		}

		[DisableObjectAccessChecks]
		private int FindEntry(TKey key)
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				for (int i = mBuckets[hashCode % mAllocSize]; i >= 0; i = mEntries[i].mNext)
				{
					if (mEntries[i].mHashCode == hashCode && (mEntries[i].mKey == key)) return i;
				}
			}
			return -1;
		}

		public bool CheckEq(TKey key, TKey key2)
		{
			return key == key2;
		}

		private int FindEntryAlt<TAltKey>(TAltKey key) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				for (int_cosize i = mBuckets[hashCode % mAllocSize]; i >= 0; i = mEntries[i].mNext)
				{
					if (mEntries[i].mHashCode == hashCode && (mEntries[i].mKey == key)) return i;
				}
			}
			return -1;
		}

		private void Initialize(int capacity)
		{
			int_cosize size = GetPrimeish((int_cosize)capacity);
			(mEntries, mBuckets) = Alloc(size);
			for (int_cosize i < (int_cosize)size) mBuckets[i] = -1;
			mAllocSize = size;
			mFreeList = -1;
		}

		private void Insert(TKey key, TValue value, bool add)
		{
			if (mBuckets == null) Initialize(0);
			int_cosize hashCode = GetKeyHash(key.GetHashCode());
			int targetBucket = hashCode % mAllocSize;

			for (int i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
			{
				if (mEntries[i].mHashCode == hashCode && (mEntries[i].mKey == key))
				{
					if (add)
					{
						Runtime.FatalError("Adding duplicate");
					}
					mEntries[i].mValue = value;
#if VERSION_DICTIONARY
					mVersion++;
#endif
					return;
				} 
		    }
			int_cosize index;
			Entry* oldData = null;
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
					oldData = Resize(false);
					targetBucket = hashCode % (int_cosize)mAllocSize;
				}
				index = mCount;
				mCount++;
			}

			mEntries[index].mHashCode = hashCode;
			mEntries[index].mNext = mBuckets[targetBucket];
			mEntries[index].mKey = key;
			mEntries[index].mValue = value;
			mBuckets[targetBucket] = index;
			if (oldData != null)
				Free(oldData);
#if VERSION_DICTIONARY
			mVersion++;
#endif
		}

		private bool Insert(TKey key, bool add, out TKey* keyPtr, out TValue* valuePtr, Entry** outOldData)
		{
			if (mBuckets == null) Initialize(0);
			int_cosize hashCode = GetKeyHash(key.GetHashCode());
			int_cosize targetBucket = hashCode % (int_cosize)mAllocSize;
			for (int_cosize i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
			{
				if (mEntries[i].mHashCode == hashCode && (mEntries[i].mKey == key))
				{
					if (add)
					{
						Runtime.FatalError("Adding duplicate entry");
					}
					keyPtr = &mEntries[i].mKey;
					valuePtr = &mEntries[i].mValue;
					if (outOldData != null)
						*outOldData = null;
					return false;
				} 
            }
			int_cosize index;
			Entry* oldData = null;
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
					oldData = Resize(false);
					targetBucket = hashCode % (int_cosize)mAllocSize;
				}
				index = mCount;
				mCount++;
			}

			mEntries[index].mHashCode = hashCode;
			mEntries[index].mNext = mBuckets[targetBucket];
			mEntries[index].mKey = key;
			mBuckets[targetBucket] = index;
#if VERSION_DICTIONARY
			mVersion++;
#endif

			keyPtr = &mEntries[index].mKey;
			valuePtr = &mEntries[index].mValue;
			if (outOldData != null)
				*outOldData = oldData;
			else
				Free(oldData);
			return true;
        }

		private bool InsertAlt<TAltKey>(TAltKey key, bool add, out TKey* keyPtr, out TValue* valuePtr, Entry** outOldData) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			if (mBuckets == null) Initialize(0);
			int_cosize hashCode = GetKeyHash(key.GetHashCode());
			int targetBucket = hashCode % (int_cosize)mAllocSize;
			for (int i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
			{
				if (mEntries[i].mHashCode == hashCode && (mEntries[i].mKey == key))
				{
					if (add)
					{
						Runtime.FatalError("Adding duplicate entry");
					}
					keyPtr = &mEntries[i].mKey;
					valuePtr = &mEntries[i].mValue;
					if (outOldData != null)
						*outOldData = null;
					return false;
				} 
		    }
			int_cosize index;
			Entry* oldData = null;
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
					oldData = Resize(false);
					targetBucket = hashCode % (int_cosize)mAllocSize;
				}
				index = mCount;
				mCount++;
			}

			mEntries[index].mHashCode = hashCode;
			mEntries[index].mNext = mBuckets[targetBucket];
			mBuckets[targetBucket] = index;
#if VERSION_DICTIONARY
			mVersion++;
#endif

			keyPtr = &mEntries[index].mKey;
			valuePtr = &mEntries[index].mValue;
			if (outOldData != null)
				*outOldData = oldData;
			else
				Free(oldData);
			return true;
		}

		// Close to prime but faster to compute
		int_cosize GetPrimeish(int_cosize min)
		{
			// This is a minimal effort to help address-aligned data
			return (min | 1);
		}

		int_cosize ExpandSize(int_cosize oldSize)
		{
			int_cosize newSize = 2 * oldSize;
            return GetPrimeish(newSize);
		}

		private Entry* Resize(bool autoFree)
		{
			return Resize(ExpandSize(mCount), false, autoFree);
		}

		private Entry* Resize(int newSize, bool forceNewHashCodes, bool autoFree)
		{
			Contract.Assert(newSize >= mAllocSize);
			(var newEntries, var newBuckets) = Alloc(newSize);
			for (int_cosize i = 0; i < newSize; i++) newBuckets[i] = -1;
			Internal.MemCpy(newEntries, mEntries, mCount * strideof(Entry), alignof(Entry));

			if (forceNewHashCodes)
			{
				for (int_cosize i = 0; i < mCount; i++)
				{
					if (newEntries[i].mHashCode != -1)
					{
						newEntries[i].mHashCode = GetKeyHash(newEntries[i].mKey.GetHashCode());
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

			let oldPtr = mEntries;
			mBuckets = newBuckets;
			mEntries = newEntries;
			mAllocSize = (int_cosize)newSize;

			if (autoFree)
			{
				Free(oldPtr);
				return null;
			}
			return oldPtr;
		}

		private bool RemoveEntry(int32 hashCode, int_cosize index)
		{
			if (mBuckets != null)
			{
				int bucket = hashCode % (int_cosize)mAllocSize;
				int lastIndex = -1;

				for (int_cosize i = mBuckets[bucket]; i >= 0; lastIndex = i, i = mEntries[i].mNext)
				{
					if (i == index)
					{
						if (lastIndex < 0)
						{
							mBuckets[bucket] = mEntries[index].mNext;
						}
						else
						{
							mEntries[lastIndex].mNext = mEntries[index].mNext;
						}
						mEntries[index].mHashCode = -1;
						mEntries[index].mNext = mFreeList;
#if BF_ENABLE_REALTIME_LEAK_CHECK
						mEntries[index].mKey = default;
						mEntries[index].mValue = default;
#endif
						mFreeList = index;
						mFreeCount++;
#if VERSION_DICTIONARY
						mVersion++;
#endif
						return true;
					}
				}
			}
			return false;
		}

		public bool Remove(TKey key)
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				int bucket = hashCode % (int_cosize)mAllocSize;
				int last = -1;
				for (int_cosize i = mBuckets[bucket]; i >= 0; last = i,i = mEntries[i].mNext)
				{
					if ((mEntries[i].mHashCode == hashCode) && (mEntries[i].mKey == key))
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
#if BF_ENABLE_REALTIME_LEAK_CHECK
						mEntries[i].mKey = default;
						mEntries[i].mValue = default;
#endif
						mFreeList = i;
						mFreeCount++;
#if VERSION_DICTIONARY
						mVersion++;
#endif
						return true;
					}
				}
			}
			return false;
		}

		void IDictionary.Remove(Variant key)
		{
			Remove(key.Get<TKey>());
		}

		public bool RemoveAlt<TAltKey>(TAltKey key) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				int bucket = hashCode % (int_cosize)mAllocSize;
				int last = -1;
				for (int_cosize i = mBuckets[bucket]; i >= 0; last = i,i = mEntries[i].mNext)
				{
					if ((mEntries[i].mHashCode == hashCode) && (mEntries[i].mKey == key))
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
#if BF_ENABLE_REALTIME_LEAK_CHECK
						mEntries[i].mKey = default;
						mEntries[i].mValue = default;
#endif
						mFreeList = i;
						mFreeCount++;
#if VERSION_DICTIONARY
						mVersion++;
#endif
						return true;
					}
				}
			}
			return false;
		}
		
		[Inline]
		public bool Remove(KeyValuePair kvPair)
		{
			return Remove(kvPair.key);
		}

		public Result<(TKey key, TValue value)> GetAndRemove(TKey key)
		{
			if (mBuckets != null)
			{
				
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				int bucket = hashCode % (int_cosize)mAllocSize;
				int_cosize last = -1;
				for (int_cosize i = mBuckets[bucket]; i >= 0; last = i,i = mEntries[i].mNext)
				{
					if ((mEntries[i].mHashCode == hashCode) && (mEntries[i].mKey == key))
					{
						if (last < 0)
						{
							mBuckets[bucket] = mEntries[i].mNext;
						}
						else
						{
							mEntries[last].mNext = mEntries[i].mNext;
						}
						TKey entryKey = mEntries[i].mKey;
						TValue result = mEntries[i].mValue;

						mEntries[i].mHashCode = -1;
						mEntries[i].mNext = mFreeList;
#if BF_ENABLE_REALTIME_LEAK_CHECK
						mEntries[i].mKey = default(TKey);
						mEntries[i].mValue = default(TValue);
#endif
						mFreeList = i;
						mFreeCount++;
#if VERSION_DICTIONARY
						mVersion++;
#endif
						return .Ok((entryKey, result));
					}
				}
			}
			return .Err;
		}

		public Result<(TKey key, TValue value)> GetAndRemoveAlt<TAltKey>(TAltKey key) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			if (mBuckets != null)
			{
				
				int_cosize hashCode = GetKeyHash(key.GetHashCode());
				int bucket = hashCode % (int_cosize)mAllocSize;
				int_cosize last = -1;
				for (int_cosize i = mBuckets[bucket]; i >= 0; last = i,i = mEntries[i].mNext)
				{
					if ((mEntries[i].mHashCode == hashCode) && (mEntries[i].mKey == key))
					{
						if (last < 0)
						{
							mBuckets[bucket] = mEntries[i].mNext;
						}
						else
						{
							mEntries[last].mNext = mEntries[i].mNext;
						}
						TKey entryKey = mEntries[i].mKey;
						TValue result = mEntries[i].mValue;

						mEntries[i].mHashCode = -1;
						mEntries[i].mNext = mFreeList;
#if BF_ENABLE_REALTIME_LEAK_CHECK
						mEntries[i].mKey = default(TKey);
						mEntries[i].mValue = default(TValue);
#endif
						mFreeList = i;
						mFreeCount++;
#if VERSION_DICTIONARY
						mVersion++;
#endif
						return .Ok((entryKey, result));
					}
				}
			}
			return .Err;
		}

		public bool TryGetValue(TKey key, out TValue value)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0)
			{
				value = mEntries[i].mValue;
				return true;
			}
			value = default(TValue);
			return false;
		}

		public bool TryGetValueAlt<TAltKey>(TAltKey key, out TValue value) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			int_cosize i = (int_cosize)FindEntryAlt<TAltKey>(key);
			if (i >= 0)
			{
				value = mEntries[i].mValue;
				return true;
			}
			value = default(TValue);
			return false;
		}

		public bool TryGet(TKey key, out TKey matchKey, out TValue value)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0)
			{
				matchKey = mEntries[i].mKey;
				value = mEntries[i].mValue;
				return true;
			}
			matchKey = default(TKey);
			value = default(TValue);
			return false;
		}

		public bool TryGetRef(TKey key, out TKey* matchKey, out TValue* value)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0)
			{
				matchKey = &mEntries[i].mKey;
				value = &mEntries[i].mValue;
				return true;
			}
			matchKey = null;
			value = null;
			return false;
		}

		public bool TryGetAlt<TAltKey>(TAltKey key, out TKey matchKey, out TValue value) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			int_cosize i = (int_cosize)FindEntryAlt(key);
			if (i >= 0)
			{
				matchKey = mEntries[i].mKey;
				value = mEntries[i].mValue;
				return true;
			}
			matchKey = default(TKey);
			value = default(TValue);
			return false;
		}

		public bool TryGetRefAlt<TAltKey>(TAltKey key, out TKey* matchKey, out TValue* value) where TAltKey : IHashable where bool : operator TKey == TAltKey
		{
			int_cosize i = (int_cosize)FindEntryAlt(key);
			if (i >= 0)
			{
				matchKey = &mEntries[i].mKey;
				value = &mEntries[i].mValue;
				return true;
			}
			matchKey = null;
			value = null;
			return false;
		}

		public TValue GetValueOrDefault(TKey key)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0)
			{
				return mEntries[i].mValue;
			}
			return default(TValue);
		}

		public struct Enumerator : IEnumerator<KeyValuePair>, IRefEnumerator<KeyRefValuePair>
		{
			private Dictionary<TKey, TValue> mDictionary;
#if VERSION_DICTIONARY
			private int_cosize mVersion;
#endif
			private int_cosize mIndex;
			private int_cosize mCurrentIndex;
			//private KeyValuePair<TKey, TValue> current;
			private int_cosize mGetEnumeratorRetType;  // What should Enumerator.Current return?

			const int_cosize DictEntry = 1;
			const int_cosize KeyValuePair = 2;

			public this(Dictionary<TKey, TValue> dictionary, int_cosize getEnumeratorRetType)
			{
				mDictionary = dictionary;
#if VERSION_DICTIONARY
				mVersion = dictionary.mVersion;
#endif
				mIndex = 0;
				mGetEnumeratorRetType = getEnumeratorRetType;
				//current = KeyValuePair<TKey, TValue>();
				//current = default(KeyValuePair<TKey, TValue>);
				mCurrentIndex = -1;
			}

#if VERSION_DICTIONARY
			void CheckVersion()
			{
				if (mVersion != mDictionary.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

			public bool MoveNext() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif
                // Use unsigned comparison since we set index to dictionary.count+1 when the enumeration ends.
                // dictionary.count+1 could be negative if dictionary.count is Int32.MaxValue
				while ((uint)mIndex < (uint)mDictionary.mCount)
				{
					if (mDictionary.mEntries[mIndex].mHashCode >= 0)
					{
						mCurrentIndex = mIndex;
						//current = KeyValuePair<TKey, TValue>(dictionary.entries[index].key, dictionary.entries[index].value);
						mIndex++;
						return true;
					}
					mIndex++;
				}

				mIndex = mDictionary.mCount + 1;
				//current = default(KeyValuePair<TKey, TValue>);
				mCurrentIndex = -1;
				return false;
			}

			public ref TKey Key
			{
				get
				{
					return ref mDictionary.mEntries[mCurrentIndex].mKey;
				}
			}

			public ref TValue Value
			{
				get
				{
					return ref mDictionary.mEntries[mCurrentIndex].mValue;
				}
			}

			public KeyValuePair Current
			{
				get { return (mDictionary.mEntries[mCurrentIndex].mKey, mDictionary.mEntries[mCurrentIndex].mValue); }
			}

			public KeyRefValuePair CurrentRef
			{
				get { return (mDictionary.mEntries[mCurrentIndex].mKey, &mDictionary.mEntries[mCurrentIndex].mValue); }
			}

			public void Dispose()
			{

			}

			public void SetValue(TValue value)
			{
				mDictionary.mEntries[mCurrentIndex].mValue = value;
			}

			public void Remove() mut
			{
				int_cosize curIdx = mIndex - 1;
				mDictionary.RemoveEntry(mDictionary.mEntries[curIdx].mHashCode, curIdx);
#if VERSION_DICTIONARY
				mVersion = mDictionary.mVersion;
#endif
				mIndex = curIdx;
			}

			public void Reset() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif

				mIndex = 0;
				//current = default(KeyValuePair<TKey, TValue>);
				mCurrentIndex = -1;
			}

			/*DictionaryEntry IDictionaryEnumerator.Entry
			{
				get
				{
					if (index == 0 || (index == dictionary.count + 1))
					{
						ThrowHelper.ThrowInvalidOperationException(ExceptionResource.InvalidOperation_EnumOpCantHappen);
					}

					return new DictionaryEntry(current.Key, current.Value);
				}
			}

			object IDictionaryEnumerator.Key
			{
				get
				{
					if (index == 0 || (index == dictionary.count + 1))
					{
						ThrowHelper.ThrowInvalidOperationException(ExceptionResource.InvalidOperation_EnumOpCantHappen);
					}

					return current.Key;
				}
			}

			object IDictionaryEnumerator.Value
			{
				get
				{
					if (index == 0 || (index == dictionary.count + 1))
					{
						ThrowHelper.ThrowInvalidOperationException(ExceptionResource.InvalidOperation_EnumOpCantHappen);
					}

					return current.Value;
				}
			}*/

			public Result<KeyValuePair> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}

			public Result<KeyRefValuePair> GetNextRef() mut
			{
				if (!MoveNext())
					return .Err;
				return CurrentRef;
			}
		}

		public struct ValueEnumerator : IRefEnumerator<TValue*>, IEnumerator<TValue>, IResettable
		{
			private Dictionary<TKey, TValue> mDictionary;
#if VERSION_DICTIONARY
			private int_cosize mVersion;
#endif
			private int_cosize mIndex;
			private TValue* mCurrent;

			const int_cosize cDictEntry = 1;
			const int_cosize cKeyValuePair = 2;

			public this(Dictionary<TKey, TValue> dictionary)
			{
				mDictionary = dictionary;
#if VERSION_DICTIONARY
				mVersion = dictionary.mVersion;
#endif
				mIndex = 0;
				mCurrent = default;
			}

			public bool MoveNext() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif

                // Use unsigned comparison since we set index to dictionary.count+1 when the enumeration ends.
                // dictionary.count+1 could be negative if dictionary.count is Int32.MaxValue
				while ((uint)mIndex < (uint)mDictionary.mCount)
				{
					if (mDictionary.mEntries[mIndex].mHashCode >= 0)
					{
						mCurrent = &mDictionary.mEntries[mIndex].mValue;
						mIndex++;
						return true;
					}
					mIndex++;
				}

				mIndex = mDictionary.mCount + 1;
				mCurrent = default;
				return false;
			}

			public TValue Current
			{
				get { return *mCurrent; }
			}

			public ref TValue CurrentRef
			{
				get mut { return ref *mCurrent; } 
			}

			public ref TKey Key
			{
				get
				{
					return ref mDictionary.mEntries[mIndex - 1].mKey;
				}
			}

#if VERSION_DICTIONARY
			void CheckVersion()
			{
				if (mVersion != mDictionary.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

			public void Dispose()
			{
			}

			public void Remove() mut
			{
				int_cosize curIdx = mIndex - 1;
				mDictionary.RemoveEntry(mDictionary.mEntries[curIdx].mHashCode, curIdx);
#if VERSION_DICTIONARY
				mVersion = mDictionary.mVersion;
#endif
				mIndex = curIdx;
			}

			public void Reset() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif

				mIndex = 0;
				mCurrent = default;
			}

			public Result<TValue> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}

			public Result<TValue*> GetNextRef() mut
			{
				if (!MoveNext())
					return .Err;
				return &CurrentRef;
			}
		}

		public struct KeyEnumerator : IEnumerator<TKey>, IRefEnumerator<TKey*>, IResettable
		{
			private Dictionary<TKey, TValue> mDictionary;
#if VERSION_DICTIONARY
			private int32 mVersion;
#endif
			private int_cosize mIndex;
			private TKey* mCurrent;

			const int_cosize DictEntry = 1;
			const int_cosize KeyValuePair = 2;

			public this(Dictionary<TKey, TValue> dictionary)
			{
				mDictionary = dictionary;
#if VERSION_DICTIONARY
				mVersion = dictionary.mVersion;
#endif
				mIndex = 0;
				mCurrent = null;
			}

#if VERSION_DICTIONARY
			void CheckVersion()
			{
				if (mVersion != mDictionary.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

			public bool MoveNext() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif

		        // Use unsigned comparison since we set index to dictionary.count+1 when the enumeration ends.
		        // dictionary.count+1 could be negative if dictionary.count is Int32.MaxValue
				while ((uint32)mIndex < (uint32)mDictionary.mCount)
				{
					if (mDictionary.mEntries[mIndex].mHashCode >= 0)
					{
						mCurrent = &mDictionary.mEntries[mIndex].mKey;
						mIndex++;
						return true;
					}
					mIndex++;
				}

				mIndex = mDictionary.mCount + 1;
				mCurrent = null;
				return false;
			}

			public TKey Current
			{
				get { return *mCurrent; }
			}

			public ref TKey CurrentRef
			{
				get { return ref *mCurrent; }
			}

			public ref TValue Value
			{
				get
				{
					return ref mDictionary.mEntries[mIndex - 1].mValue;
				}
			}

			public void Dispose()
			{
			}

			public void Remove() mut
			{
				int_cosize curIdx = mIndex - 1;
				mDictionary.RemoveEntry(mDictionary.mEntries[curIdx].mHashCode, curIdx);
#if VERSION_DICTIONARY
				mVersion = mDictionary.mVersion;
#endif
				mIndex = curIdx;
			}

			public void Reset() mut
			{
#if VERSION_DICTIONARY
				CheckVersion();
#endif
				mIndex = 0;
				mCurrent = null;
			}

			public Result<TKey> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}

			public Result<TKey*> GetNextRef() mut
			{
				if (!MoveNext())
					return .Err;
				return &CurrentRef;
			}
		}
	}
}
