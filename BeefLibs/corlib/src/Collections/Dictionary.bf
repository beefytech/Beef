// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_DICTIONARY
#endif

namespace System.Collections
{
	using System;
	using System.Collections;
	using System.Diagnostics;
	using System.Diagnostics.Contracts;

	public class Dictionary<TKey, TValue> : ICollection<KeyValuePair<TKey, TValue>> where TKey : IHashable //: IDictionary<TKey, TValue>, IDictionary, IReadOnlyDictionary<TKey, TValue>, ISerializable, IDeserializationCallback
	{
		private struct Entry			
		{
			public TKey mKey;           // Key of entry
			public TValue mValue;         // Value of entry
			public int32 mHashCode;    // Lower 31 bits of hash code, -1 if unused
			public int_cosize mNext;        // Index of next entry, -1 if last
		}

		int_cosize* mBuckets ~ delete _;
		Entry* mEntries ~ delete _;
		int_cosize mAllocSize;
		int_cosize mCount;
		int_cosize mFreeList;
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

		public void Add(TKey key, TValue value)
		{
			Insert(key, value, true);
		}
		
		public void Add(KeyValuePair<TKey, TValue> kvPair)
		{
			Insert(kvPair.Key, kvPair.Value, true);
		}

		public bool TryAdd(TKey key, TValue value)
		{
			TKey* keyPtr;
			TValue* valuePtr;
			bool inserted = Insert(key, false, out keyPtr, out valuePtr);
			if (!inserted)
				return false;
			*keyPtr = key;
			*valuePtr = value;
			return true;
		}

		public bool TryAdd(TKey key, out TKey* keyPtr, out TValue* valuePtr)
		{
            return Insert(key, false, out keyPtr, out valuePtr);
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
			if (Insert(key, false, out keyPtr, out valuePtr))
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

		public bool ContainsValue(TValue value)
		{
			if (value == null)
			{
				for (int_cosize i = 0; i < mCount; i++)
				{
					if (mEntries[i].mHashCode >= 0 && mEntries[i].mValue == null) return true;
				}
			}
			else
			{
				//TODO: IMPORTANT!
				/*EqualityComparer<TValue> c = EqualityComparer<TValue>.Default;
				for (int i = 0; i < count; i++)
				{
					if (entries[i].hashCode >= 0 && c.Equals(entries[i].value, value)) return true;
				}*/
			}
			return false;
		}
		
		public bool Contains(KeyValuePair<TKey, TValue> kvPair)
		{
			TValue value;
			if(TryGetValue(kvPair.Key, out value))
			{
				return value == kvPair.Value;
			}else{
				return false;
			}
		}
		
		public void CopyTo(KeyValuePair<TKey, TValue>[] kvPair, int index)
		{
			Keys.Reset();
			Values.Reset();
			int i = 0;

			repeat
			{
				if(i >= index)
				{
					kvPair[i] = KeyValuePair<TKey,TValue>(Keys.Current, Values.CurrentRef);
				}
			}
			while(Keys.MoveNext() && Values.MoveNext());

			Keys.Reset();
			Values.Reset();
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this, Enumerator.[Friend]KeyValuePair);
		}

		[DisableObjectAccessChecks]
		private int FindEntry(TKey key)
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = (int_cosize)key.GetHashCode() & 0x7FFFFFFF;
				for (int_cosize i = mBuckets[hashCode % mAllocSize]; i >= 0; i = mEntries[i].mNext)
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

		private int FindEntryWith<TAltKey>(TAltKey key) where TAltKey : IOpEquals<TKey>, IHashable
		{
			if (mBuckets != null)
			{
				int_cosize hashCode = (int_cosize)key.GetHashCode() & 0x7FFFFFFF;
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
			mBuckets = new int_cosize[size]*;
			for (int_cosize i < (int_cosize)size) mBuckets[i] = -1;
			mEntries = new Entry[size]*;
			mAllocSize = size;
			mFreeList = -1;
		}

		private void Insert(TKey key, TValue value, bool add)
		{
			if (mBuckets == null) Initialize(0);
			int32 hashCode = (int32)key.GetHashCode() & 0x7FFFFFFF;
			int_cosize targetBucket = hashCode % (int_cosize)mAllocSize;

			for (int_cosize i = mBuckets[targetBucket]; i >= 0; i = mEntries[i].mNext)
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
			mEntries[index].mKey = key;
			mEntries[index].mValue = value;
			mBuckets[targetBucket] = index;
#if VERSION_DICTIONARY
			mVersion++;
#endif
		}

		private bool Insert(TKey key, bool add, out TKey* keyPtr, out TValue* valuePtr)
		{
			if (mBuckets == null) Initialize(0);
			int32 hashCode = (int32)key.GetHashCode() & 0x7FFFFFFF;
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
			mEntries[index].mKey = key;
			mBuckets[targetBucket] = index;
#if VERSION_DICTIONARY
			mVersion++;
#endif

			keyPtr = &mEntries[index].mKey;
			valuePtr = &mEntries[index].mValue;
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

		private void Resize()
		{
			Resize(ExpandSize(mCount), false);
		}

		private void Resize(int newSize, bool forceNewHashCodes)
		{
			Contract.Assert(newSize >= mAllocSize);
			int_cosize* newBuckets = new int_cosize[newSize]*;
			for (int_cosize i = 0; i < newSize; i++) newBuckets[i] = -1;
			Entry* newEntries = new Entry[newSize]*;
			Internal.MemCpy(newEntries, mEntries, mCount * strideof(Entry), alignof(Entry));

			if (forceNewHashCodes)
			{
				for (int_cosize i = 0; i < mCount; i++)
				{
					if (newEntries[i].mHashCode != -1)
					{
						newEntries[i].mHashCode = (int32)newEntries[i].mKey.GetHashCode() & 0x7FFFFFFF;
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

			delete mBuckets;
			delete mEntries;

			mBuckets = newBuckets;
			mEntries = newEntries;
			mAllocSize = (int_cosize)newSize;
		}

		public bool Remove(TKey key)
		{
			if (key == null)
			{
				ThrowUnimplemented();
				//ThrowHelper.ThrowArgumentNullException(ExceptionArgument.key);
			}

			if (mBuckets != null)
			{
				
				int32 hashCode = (int32)key.GetHashCode() & 0x7FFFFFFF;
				int_cosize bucket = hashCode % (int_cosize)mAllocSize;
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
		public bool Remove(KeyValuePair<TKey, TValue> kvPair)
		{
			return Remove(kvPair.Key);
		}

		public Result<(TKey key, TValue value)> GetAndRemove(TKey key)
		{
			if (mBuckets != null)
			{
				
				int_cosize hashCode = (int_cosize)key.GetHashCode() & 0x7FFFFFFF;
				int_cosize bucket = hashCode % (int_cosize)mAllocSize;
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

		public bool TryGetValue(TKey key, out TKey matchKey, out TValue value)
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

		public bool TryGetWith<TAltKey>(TAltKey key, out TKey matchKey, out TValue value) where TAltKey : IOpEquals<TKey>, IHashable
		{
			int_cosize i = (int_cosize)FindEntryWith(key);
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

		public TValue GetValueOrDefault(TKey key)
		{
			int_cosize i = (int_cosize)FindEntry(key);
			if (i >= 0)
			{
				return mEntries[i].mValue;
			}
			return default(TValue);
		}

		private static bool IsCompatibleKey(Object key)
		{
			if (key == null)
			{
				ThrowUnimplemented();
				//ThrowHelper.ThrowArgumentNullException(ExceptionArgument.key);
			}
			return (key is TKey);
		}

		public struct Enumerator : IEnumerator<(TKey key, TValue value)>//, IDictionaryEnumerator
		{
			private Dictionary<TKey, TValue>  mDictionary;
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

			public (TKey key, TValue value) Current
			{
				get { return (mDictionary.mEntries[mCurrentIndex].mKey, mDictionary.mEntries[mCurrentIndex].mValue); }
			}

			public void Dispose()
			{
			}

			public void SetValue(TValue value)
			{
				mDictionary.mEntries[mCurrentIndex].mValue = value;
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

			public Result<(TKey key, TValue value)> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct ValueEnumerator : IRefEnumerator<TValue>, IResettable
		{
			private Dictionary<TKey, TValue> mDictionary;
#if VERSION_DICTIONARY
			private int_cosize mVersion;
#endif
			private int_cosize mIndex;
			private TValue mCurrent;

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
						mCurrent = mDictionary.mEntries[mIndex].mValue;
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
				get { return mCurrent; }
			}

			public ref TValue CurrentRef
			{
				get mut { return ref mCurrent; } 
			}

			public ref TKey Key
			{
				get
				{
					return ref mDictionary.mEntries[mIndex].mKey;
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

		public struct KeyEnumerator : IEnumerator<TKey>, IResettable
		{
			private Dictionary<TKey, TValue> mDictionary;
# if VERSION_DICTIONARY
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
		}
	}
}

namespace System.Collections.Generic
{
	[Obsolete("The System.Collections.Generic types have been moved into System.Collections", false)]
	typealias Dictionary<TKey, TValue> = System.Collections.Dictionary<TKey, TValue>;
}
