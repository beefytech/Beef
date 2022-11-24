// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_HASHSET
#endif

using System;
using System.Collections;
using System.Collections;
using System.Diagnostics;
using System.Diagnostics.Contracts;
using System.Text;
using System.Security;

namespace System.Collections
{
	/// Implementation notes:
	/// This uses an array-based implementation similar to Dictionary<T>, using a buckets array
	/// to map hash values to the Slots array. Items in the Slots array that hash to the same value
	/// are chained together through the "next" indices.
	///
	/// The capacity is always prime; so during resizing, the capacity is chosen as the next prime
	/// greater than double the last capacity.
	///
	/// The underlying data structures are lazily initialized. Because of the observation that,
	/// in practice, hashtables tend to contain only a few elements, the initial capacity is
	/// set very small (3 elements) unless the ctor with a collection is used.
	///
	/// The +/- 1 modifications in methods that add, check for containment, etc allow us to
	/// distinguish a hash code of 0 from an uninitialized bucket. This saves us from having to
	/// reset each bucket to -1 when resizing. See Contains, for example.
	///
	/// Set methods such as UnionWith, IntersectWith, ExceptWith, and SymmetricExceptWith modify
	/// this set.
	///
	/// Some operations can perform faster if we can assume "other" contains unique elements
	/// according to this equality comparer. The only times this is efficient to check is if
	/// other is a hashset. Note that checking that it's a hashset alone doesn't suffice; we
	/// also have to check that the hashset is using the same equality comparer. If other
	/// has a different equality comparer, it will have unique elements according to its own
	/// equality comparer, but not necessarily according to ours. Therefore, to go these
	/// optimized routes we check that other is a hashset using the same equality comparer.
	///
	/// A HashSet with no elements has the properties of the empty set. (See IsSubset, etc. for
	/// special empty set checks.)
	///
	/// A couple of methods have a special case if other is this (e.g. SymmetricExceptWith).
	/// If we didn't have these checks, we could be iterating over the set and modifying at
	/// the same time.
	
	public class HashSet<T> : IEnumerable<T> //: ICollection<T>, ISerializable, IDeserializationCallback, ISet<T>, IReadOnlyCollection<T>
		where T : IHashable
	{
		// store lower 31 bits of hash code
		private const int32 Lower31BitMask = 0x7FFFFFFF;
		// cutoff point, above which we won't do stackallocs. This corresponds to 100 integers.
		private const int32 StackAllocThreshold = 100;
		// when constructing a hashset from an existing collection, it may contain duplicates, 
		// so this is used as the max acceptable excess ratio of capacity to count. Note that
		// this is only used on the ctor and not to automatically shrink if the hashset has, e.g,
		// a lot of adds followed by removes. Users must explicitly shrink by calling TrimExcess.
		// This is set to 3 because capacity is acceptable as 2x rounded up to nearest prime.
		private const int32 ShrinkThreshold = 3;

		private int32[] mBuckets ~ delete _;
		private Slot[] mSlots ~ delete _;
		private int32 mCount;
		private int32 mLastIndex;
		private int32 mFreeList;
#if VERSION_HASHSET
		private int32 mVersion;
		const String cVersionError = "HashSet changed during enumeration";
#endif

		public this()
		{
			mLastIndex = 0;
			mCount = 0;
			mFreeList = -1;
#if VERSION_HASHSET
			mVersion = 0;
#endif
		}

		public this(int wantSize)
		{
			mLastIndex = 0;
			mCount = 0;
			mFreeList = -1;
#if VERSION_HASHSET
			mVersion = 0;
#endif
			Initialize((int32)wantSize);
		}

		/*public this()
            : this(EqualityComparer<T>.Default) { }*/

		/*public this(IEqualityComparer<T> comparer)
		{
			if (comparer == null)
			{
				comparer = EqualityComparer<T>.Default;
			}

			this.m_comparer = comparer;
			m_lastIndex = 0;
			m_count = 0;
			m_freeList = -1;
			m_version = 0;
		}

		public this(IEnumerable<T> collection)
            : this(collection, EqualityComparer<T>.Default) { }

		/// Implementation Notes:
		/// Since resizes are relatively expensive (require rehashing), this attempts to minimize 
		/// the need to resize by setting the initial capacity based on size of collection. 
		public this(IEnumerable<T> collection, IEqualityComparer<T> comparer)
            : this(comparer)
		{
			if (collection == null)
			{
				Runtime.FatalError();
			}
			Contract.EndContractBlock();

			// to avoid excess resizes, first set size based on collection's count. Collection
			// may contain duplicates, so call TrimExcess if resulting hashset is larger than
			// threshold
			int suggestedCapacity = 0;
			ICollection<T> coll = collection as ICollection<T>;
			if (coll != null)
			{
				suggestedCapacity = coll.Count;
			}
			Initialize(suggestedCapacity);

			this.UnionWith(collection);
			if ((m_count == 0 && m_slots.Length > HashHelpers.GetMinPrime()) ||
			(m_count > 0 && m_slots.Length / m_count > ShrinkThreshold))
			{
				TrimExcess();
			}
		}*/

		/// Add item to this hashset. This is the explicit implementation of the ICollection<T>
		/// interface. The other Add method returns bool indicating whether item was added.
		/// <param name="item">item to add</param>
		/*void ICollection<T>.Add(T item)
		{
			AddIfNotPresent(item);
		}
		  */
		/// Remove all items from this set. This clears the elements but not the underlying 
		/// buckets and slots array. Follow this call by TrimExcess to release these.
		public void Clear()
		{
			if (mLastIndex > 0)
			{
				Debug.Assert(mBuckets != null, "m_buckets was null but m_lastIndex > 0");

				// clear the elements so that the gc can reclaim the references.
				// clear only up to m_lastIndex for m_slots
				Array.Clear(mSlots, 0, mLastIndex);
				Array.Clear(mBuckets, 0, mBuckets.Count);
				mLastIndex = 0;
				mCount = 0;
				mFreeList = -1;
			}
#if VERSION_HASHSET
			mVersion++;
#endif
		}

		/// Checks if this hashset contains the item
		/// @param item item to check for containment
		/// @return true if item contained; false if not
		public bool Contains(T item)
		{
			if (mBuckets != null)
			{
				int32 hashCode = (int32)InternalGetHashCode(item);
				// see note at "HashSet" level describing why "- 1" appears in for loop
				for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
				{
					if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == item))
					{
						return true;
					}
				}
			}
			// either m_buckets is null or wasn't found
			return false;
		}

		[Obsolete("Method renamed to ContainsAlt", false)]
		public bool ContainsWith<TAltKey>(TAltKey item) where TAltKey : IHashable where bool : operator T == TAltKey
		{
			return ContainsAlt(item);
		}

		public bool ContainsAlt<TAltKey>(TAltKey item) where TAltKey : IHashable where bool : operator T == TAltKey
		{
			if (mBuckets != null)
			{
				int32 hashCode = (int32)item.GetHashCode() & Lower31BitMask;
				// see note at "HashSet" level describing why "- 1" appears in for loop
				for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
				{
					if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == item))
					{
						return true;
					}
				}
			}
			// either m_buckets is null or wasn't found
			return false;
		}

		/// Copy items in this hashset to array, starting at arrayIndex
		/// @param array array to add items to
		/// @param arrayIndex index to start at
		public void CopyTo(T[] array, int32 arrayIndex)
		{
			CopyTo(array, arrayIndex, mCount);
		}

		private bool RemoveEntry(int32 hashCode, int_cosize index)
		{
			if (mBuckets != null)
			{
				int32 bucket = hashCode % (int32)mBuckets.Count;
				int32 last = -1;
				for (int32 i = mBuckets[bucket] - 1; i >= 0; last = i,i = mSlots[i].mNext)
				{
					if (i == index)
					{
						if (last < 0)
						{
							// first iteration; update buckets
							mBuckets[bucket] = mSlots[i].mNext + 1;
						}
						else
						{
							// subsequent iterations; update 'next' pointers
							mSlots[last].mNext = mSlots[i].mNext;
						}
						mSlots[i].mHashCode = -1;
						mSlots[i].mValue = default(T);
						mSlots[i].mNext = mFreeList;

						mCount--;
#if VERSION_HASHSET
						mVersion++;
#endif
						if (mCount == 0)
						{
							mLastIndex = 0;
							mFreeList = -1;
						}
						else
						{
							mFreeList = i;
						}
						return true;
					}
				}
			}
			// either m_buckets is null or wasn't found
			return false;
		}

		bool Remove(T item, T* outValue)
		{
			if (mBuckets != null)
			{
				int32 hashCode = (int32)InternalGetHashCode(item);
				int32 bucket = hashCode % (int32)mBuckets.Count;
				int32 last = -1;
				for (int32 i = mBuckets[bucket] - 1; i >= 0; last = i,i = mSlots[i].mNext)
				{
					if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == item))
					{
						if (last < 0)
						{
							// first iteration; update buckets
							mBuckets[bucket] = mSlots[i].mNext + 1;
						}
						else
						{
							// subsequent iterations; update 'next' pointers
							mSlots[last].mNext = mSlots[i].mNext;
						}
						if (outValue != null)
							*outValue = mSlots[i].mValue;
						mSlots[i].mHashCode = -1;
						mSlots[i].mValue = default(T);
						mSlots[i].mNext = mFreeList;

						mCount--;
#if VERSION_HASHSET
						mVersion++;
#endif
						if (mCount == 0)
						{
							mLastIndex = 0;
							mFreeList = -1;
						}
						else
						{
							mFreeList = i;
						}
						return true;
					}
				}
			}
			// either m_buckets is null or wasn't found
			return false;
		}

		/// Remove item from this container
		/// @param item item to remove
		/// @return true if removed; false if not (i.e. if the item wasn't in the HashSet)
		public bool Remove(T item)
		{
			return Remove(item, null);
		}

		/// Remove item from this container
		/// @param item item to remove
		/// @return .Ok(value) if removed, with 'value' being the stored value; .Err if not (i.e. if the item wasn't in the HashSet)
		public Result<T> GetAndRemove(T item)
		{
			T value = ?;
			if (!Remove(item, &value))
				return .Err;
			return .Ok(value);
		}

		/// Number of elements in this hashset
		public int Count
		{
			get { return mCount; }
		}

		public bool IsEmpty
		{
			get
			{
				return mCount == 0;
			}
		}

		/// Whether this is readonly
		/*bool ICollection<T>.IsReadOnly
		{
			get { return false; }
		}*/

		#endregion

		#region IEnumerable methods

		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		/*IEnumerator<T> IEnumerable<T>.GetEnumerator()
		{
			return new Enumerator(this);
		}

		IEnumerator IEnumerable.GetEnumerator()
		{
			return new Enumerator(this);
		} */

		#endregion

		#region ISerializable methods

		/// Add item to this HashSet. Returns bool indicating whether item was added (won't be 
		/// added if already present)
		/// @return true if added, false if already present
		public bool Add(T item)
		{
			T* entryPtr;
			return Add(item, out entryPtr);
		}

		public bool TryAdd(T item, out T* entryPtr)
		{
			return Add(item, out entryPtr);
		}

		public bool TryAddAlt<TAltKey>(TAltKey item, out T* entryPtr) where TAltKey : IHashable where bool : operator T == TAltKey
		{
			return AddAlt(item, out entryPtr);
		}

		public void CopyTo(T[] array) { CopyTo(array, 0, mCount); }

		public void CopyTo(T[] array, int32 arrayIndex, int32 count)
		{
			if (array == null)
			{
				Runtime.FatalError("array");
			}
			Contract.EndContractBlock();

			// check array index valid index into array
			if (arrayIndex < 0)
			{
				//throw new ArgumentOutOfRangeException("arrayIndex", SR.GetString(SR.ArgumentOutOfRange_NeedNonNegNum));
				Runtime.FatalError();
			}

			// also throw if count less than 0
			if (count < 0)
			{
				//throw new ArgumentOutOfRangeException("count", SR.GetString(SR.ArgumentOutOfRange_NeedNonNegNum));
				Runtime.FatalError();
			}

			// will array, starting at arrayIndex, be able to hold elements? Note: not
			// checking arrayIndex >= array.Length (consistency with list of allowing
			// count of 0; subsequent check takes care of the rest)
			if (arrayIndex > array.Count || count > array.Count - arrayIndex)
			{
				//throw new ArgumentException(SR.GetString(SR.Arg_ArrayPlusOffTooSmall));
				Runtime.FatalError();
			}

			int32 numCopied = 0;
			for (int32 i = 0; i < mLastIndex && numCopied < count; i++)
			{
				if (mSlots[i].mHashCode >= 0)
				{
					array[arrayIndex + numCopied] = mSlots[i].mValue;
					numCopied++;
				}
			}
		}

		/// Remove elements that match specified predicate. Returns the number of elements removed
		public int32 RemoveWhere(Predicate<T> match)
		{
			if (match == null)
			{
				Runtime.FatalError("match");
			}
			Contract.EndContractBlock();

			int32 numRemoved = 0;
			for (int32 i = 0; i < mLastIndex; i++)
			{
				if (mSlots[i].mHashCode >= 0)
				{
					// cache value in case delegate removes it
					T value = mSlots[i].mValue;
					if (match(value))
					{
						// check again that remove actually removed it
						if (Remove(value))
						{
							numRemoved++;
						}
					}
				}
			}
			return numRemoved;
		}

		/// Gets the IEqualityComparer that is used to determine equality of keys for 
		/// the HashSet.
		/*public IEqualityComparer<T> Comparer
		{
			get
			{
				return m_comparer;
			}
		} */

		int32 GetPrimeish(int32 min)
		{
			// This is a minimal effort to help address-aligned dataa
			return (min | 1);
		}

		int32 ExpandSize(int32 oldSize)
		{
			int32 newSize = 2 * oldSize;
			return GetPrimeish(newSize);
		}

		/// Sets the capacity of this list to the size of the list (rounded up to nearest prime),
		/// unless count is 0, in which case we release references.
		/// 
		/// This method can be used to minimize a list's memory overhead once it is known that no
		/// new elements will be added to the list. To completely clear a list and release all 
		/// memory referenced by the list, execute the following statements:
		/// 
		/// list.Clear();
		/// list.TrimExcess(); 
		public void TrimExcess()
		{
			Debug.Assert(mCount >= 0, "mCount is negative");

			if (mCount == 0)
			{
				// if count is zero, clear references
				delete mBuckets;
				mBuckets = null;
				delete mSlots;
				mSlots = null;
#if VERSION_HASHSET
				mVersion++;
#endif
			}
			else
			{
				Debug.Assert(mBuckets != null, "mBuckets was null but mCount > 0");

				// similar to IncreaseCapacity but moves down elements in case add/remove/etc
				// caused fragmentation
				int32 newSize = GetPrimeish(mCount);
				Slot[] newSlots = new Slot[newSize];
				int32[] newBuckets = new int32[newSize];

				// move down slots and rehash at the same time. newIndex keeps track of current 
				// position in newSlots array
				int32 newIndex = 0;
				for (int32 i = 0; i < mLastIndex; i++)
				{
					if (mSlots[i].mHashCode >= 0)
					{
						newSlots[newIndex] = mSlots[i];

						// rehash
						int32 bucket = newSlots[newIndex].mHashCode % newSize;
						newSlots[newIndex].mNext = newBuckets[bucket] - 1;
						newBuckets[bucket] = newIndex + 1;

						newIndex++;
					}
				}

				Debug.Assert(newSlots.Count <= mSlots.Count, "capacity increased after TrimExcess");

				mLastIndex = newIndex;
				delete mSlots;
				mSlots = newSlots;
				delete mBuckets;
				mBuckets = newBuckets;
				mFreeList = -1;
			}
		}

		#endregion

		#region Helper methods

		/// Initializes buckets and slots arrays. Uses suggested capacity by finding next prime
		/// greater than or equal to capacity.
		private void Initialize(int32 capacity)
		{
			Debug.Assert(mBuckets == null, "Initialize was called but mBuckets was non-null");

			int32 size = GetPrimeish(capacity);

			mBuckets = new int32[size];
			mSlots = new Slot[size];
		}

		/// Expand to new capacity. New capacity is next prime greater than or equal to suggested 
		/// size. This is called when the underlying array is filled. This performs no 
		/// defragmentation, allowing faster execution; note that this is reasonable since 
		/// AddIfNotPresent attempts to insert new elements in re-opened spots.
		private void IncreaseCapacity()
		{
			Debug.Assert(mBuckets != null, "IncreaseCapacity called on a set with no elements");

			int32 newSize = ExpandSize(mCount);
			if (newSize <= mCount)
			{
				//throw new ArgumentException(SR.GetString(SR.Arg_HSCapacityOverflow));
				Runtime.FatalError();
			}

			// Able to increase capacity; copy elements to larger array and rehash
			SetCapacity(newSize, false);
		}

		/// Set the underlying buckets array to size newSize and rehash.  Note that newSize
		/// *must* be a prime.  It is very likely that you want to call IncreaseCapacity()
		/// instead of this method.
		private void SetCapacity(int32 newSize, bool forceNewHashCodes)
		{
			//Contract.Assert(HashHelpers.IsPrime(newSize), "New size is not prime!");

			Contract.Assert(mBuckets != null, "SetCapacity called on a set with no elements");

			Slot[] newSlots = new Slot[newSize];
			if (mSlots != null)
			{
				Array.Copy(mSlots, 0, newSlots, 0, mLastIndex);
			}

			if (forceNewHashCodes)
			{
				for (int32 i = 0; i < mLastIndex; i++)
				{
					if (newSlots[i].mHashCode != -1)
					{
						newSlots[i].mHashCode = (int32)InternalGetHashCode(newSlots[i].mValue);
					}
				}
			}

			int32[] newBuckets = new int32[newSize];
			for (int32 i = 0; i < mLastIndex; i++)
			{
				int32 bucket = newSlots[i].mHashCode % newSize;
				newSlots[i].mNext = newBuckets[bucket] - 1;
				newBuckets[bucket] = i + 1;
			}
			delete mSlots;
			mSlots = newSlots;
			delete mBuckets;
			mBuckets = newBuckets;
		}

		/// Adds value to HashSet if not contained already
		/// @return true if added and false if already present
		/// @param value value to find
		/// @param entryPtr ponter to entry
		public bool Add(T value, out T* entryPtr)
		{
			if (mBuckets == null)
			{
				Initialize(0);
			}

			int32 hashCode = (int32)InternalGetHashCode(value);
			int32 bucket = hashCode % (int32)mBuckets.Count;
#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
			int collisionCount = 0;
#endif
			for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
			{
				if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == value))
				{
					entryPtr = &mSlots[i].mValue;
					return false;
				}
#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
				collisionCount++;
#endif
			}

			int32 index;
			if (mFreeList >= 0)
			{
				index = mFreeList;
				mFreeList = mSlots[index].mNext;
			}
			else
			{
				if (mLastIndex == mSlots.Count)
				{
					IncreaseCapacity();
					// this will change during resize
					bucket = hashCode % (int32)mBuckets.Count;
				}
				index = mLastIndex;
				mLastIndex++;
			}
			mSlots[index].mHashCode = hashCode;
			mSlots[index].mValue = value;
			mSlots[index].mNext = mBuckets[bucket] - 1;
			mBuckets[bucket] = index + 1;
			mCount++;
#if VERSION_HASHSET
			mVersion++;
#endif

#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
			if(collisionCount > HashHelpers.HashCollisionThreshold && HashHelpers.IsWellKnownEqualityComparer(m_comparer)) {
				m_comparer = (IEqualityComparer<T>) HashHelpers.GetRandomizedEqualityComparer(m_comparer);
				SetCapacity(m_buckets.Length, true);
			}
#endif // FEATURE_RANDOMIZED_STRING_HASHING

			entryPtr = &mSlots[index].mValue;
			return true;
		}

		/// Adds value to HashSet if not contained already
		/// @return true if added and false if already present
		/// @param value value to find
		/// @param entryPtr ponter to entry
		public bool AddAlt<TAltKey>(TAltKey value, out T* entryPtr) where TAltKey : IHashable where bool : operator T == TAltKey
		{
			if (mBuckets == null)
			{
				Initialize(0);
			}

			int32 hashCode = (int32)InternalGetHashCodeAlt(value);
			int32 bucket = hashCode % (int32)mBuckets.Count;
#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
			int collisionCount = 0;
#endif
			for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
			{
				if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == value))
				{
					entryPtr = &mSlots[i].mValue;
					return false;
				}
#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
				collisionCount++;
#endif
			}

			int32 index;
			if (mFreeList >= 0)
			{
				index = mFreeList;
				mFreeList = mSlots[index].mNext;
			}
			else
			{
				if (mLastIndex == mSlots.Count)
				{
					IncreaseCapacity();
					// this will change during resize
					bucket = hashCode % (int32)mBuckets.Count;
				}
				index = mLastIndex;
				mLastIndex++;
			}
			mSlots[index].mHashCode = hashCode;
			//mSlots[index].mValue = value;
			mSlots[index].mNext = mBuckets[bucket] - 1;
			mBuckets[bucket] = index + 1;
			mCount++;
#if VERSION_HASHSET
			mVersion++;
#endif

#if FEATURE_RANDOMIZED_STRING_HASHING && !FEATURE_NETCORE
			if(collisionCount > HashHelpers.HashCollisionThreshold && HashHelpers.IsWellKnownEqualityComparer(m_comparer)) {
				m_comparer = (IEqualityComparer<T>) HashHelpers.GetRandomizedEqualityComparer(m_comparer);
				SetCapacity(m_buckets.Length, true);
			}
#endif // FEATURE_RANDOMIZED_STRING_HASHING

			entryPtr = &mSlots[index].mValue;
			return true;
		}

		/// Checks if this contains of other's elements. Iterates over other's elements and 
		/// returns false as soon as it finds an element in other that's not in this.
		/// Used by SupersetOf, ProperSupersetOf, and SetEquals.
		/*private bool ContainsAllElements(IEnumerable<T> other)
		{
			foreach (T element in other)
			{
				if (!Contains(element))
				{
					return false;
				}
			}
			return true;
		}*/

		/// Implementation Notes:
		/// If other is a hashset and is using same equality comparer, then checking subset is 
		/// faster. Simply check that each element in this is in other.
		/// 
		/// Note: if other doesn't use same equality comparer, then Contains check is invalid,
		/// which is why callers must take are of this.
		/// 
		/// If callers are concerned about whether this is a proper subset, they take care of that.
		private bool IsSubsetOfHashSetWithSameEC(HashSet<T> other)
		{
			for (T item in this)
			{
				if (!other.Contains(item))
				{
					return false;
				}
			}
			return true;
		}

		/// If other is a hashset that uses same equality comparer, intersect is much faster 
		/// because we can use other's Contains
		private void IntersectWithHashSetWithSameEC(HashSet<T> other)
		{
			for (int32 i = 0; i < mLastIndex; i++)
			{
				if (mSlots[i].mHashCode >= 0)
				{
					T item = mSlots[i].mValue;
					if (!other.Contains(item))
					{
						Remove(item);
					}
				}
			}
		}

		/// Iterate over other. If contained in this, mark an element in bit array corresponding to
		/// its position in m_slots. If anything is unmarked (in bit array), remove it.
		/// 
		/// This attempts to allocate on the stack, if below StackAllocThreshold.
		/*private void IntersectWithEnumerable(IEnumerable<T> other)
		{
			Debug.Assert(m_buckets != null, "m_buckets shouldn't be null; callers should check first");

			// keep track of current last index; don't want to move past the end of our bit array
			// (could happen if another thread is modifying the collection)
			int originalLastIndex = m_lastIndex;
			int intArrayLength = BitHelper.ToIntArrayLength(originalLastIndex);

			BitHelper bitHelper;
			if (intArrayLength <= StackAllocThreshold)
			{
				int* bitArrayPtr = stackallocint[intArrayLength];
				bitHelper = new BitHelper(bitArrayPtr, intArrayLength);
			}
			else
			{
				int[] bitArray = new int[intArrayLength];
				bitHelper = new BitHelper(bitArray, intArrayLength);
			}

			// mark if contains: find index of in slots array and mark corresponding element in bit array
			foreach (T item in other)
			{
				int index = InternalIndexOf(item);
				if (index >= 0)
				{
					bitHelper.MarkBit(index);
				}
			}

			// if anything unmarked, remove it. Perf can be optimized here if BitHelper had a 
			// FindFirstUnmarked method.
			for (int i = 0; i < originalLastIndex; i++)
			{
				if (m_slots[i].hashCode >= 0 && !bitHelper.IsMarked(i))
				{
					Remove(m_slots[i].value);
				}
			}
		}	   */

		/// Used internally by set operations which have to rely on bit array marking. This is like
		/// Contains but returns index in slots array. 
		private int32 InternalIndexOf(T item)
		{
			Debug.Assert(mBuckets != null, "m_buckets was null; callers should check first");

			int32 hashCode = (int32)InternalGetHashCode(item);
			for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
			{
				if ((mSlots[i].mHashCode) == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == item))
				{
					return i;
				}
			}
			// wasn't found
			return -1;
		}

		/// if other is a set, we can assume it doesn't have duplicate elements, so use this
		/// technique: if can't remove, then it wasn't present in this set, so add.
		/// 
		/// As with other methods, callers take care of ensuring that other is a hashset using the
		/// same equality comparer.
		private void SymmetricExceptWithUniqueHashSet(HashSet<T> other)
		{
			for (T item in other)
			{
				if (!Remove(item))
				{
					Add(item);
				}
			}
		}

		/// Implementation notes:
		/// 
		/// Used for symmetric except when other isn't a HashSet. This is more tedious because 
		/// other may contain duplicates. HashSet technique could fail in these situations:
		/// 1. Other has a duplicate that's not in this: HashSet technique would add then 
		/// remove it.
		/// 2. Other has a duplicate that's in this: HashSet technique would remove then add it
		/// back.
		/// In general, its presence would be toggled each time it appears in other. 
		/// 
		/// This technique uses bit marking to indicate whether to add/remove the item. If already
		/// present in collection, it will get marked for deletion. If added from other, it will
		/// get marked as something not to remove.
		///
		/*private void SymmetricExceptWithEnumerable(IEnumerable<T> other)
		{
			int originalLastIndex = m_lastIndex;
			int intArrayLength = BitHelper.ToIntArrayLength(originalLastIndex);

			BitHelper itemsToRemove;
			BitHelper itemsAddedFromOther;
			if (intArrayLength <= StackAllocThreshold / 2)
			{
				int* itemsToRemovePtr = stackallocint[intArrayLength];
				itemsToRemove = new BitHelper(itemsToRemovePtr, intArrayLength);

				int* itemsAddedFromOtherPtr = stackallocint[intArrayLength];
				itemsAddedFromOther = new BitHelper(itemsAddedFromOtherPtr, intArrayLength);
			}
			else
			{
				int[] itemsToRemoveArray = new int[intArrayLength];
				itemsToRemove = new BitHelper(itemsToRemoveArray, intArrayLength);

				int[] itemsAddedFromOtherArray = new int[intArrayLength];
				itemsAddedFromOther = new BitHelper(itemsAddedFromOtherArray, intArrayLength);
			}

			foreach (T item in other)
			{
				int location = 0;
				bool added = AddOrGetLocation(item, out location);
				if (added)
				{
					// wasn't already present in collection; flag it as something not to remove
					// *NOTE* if location is out of range, we should ignore. BitHelper will
					// detect that it's out of bounds and not try to mark it. But it's
					// expected that location could be out of bounds because adding the item
					// will increase m_lastIndex as soon as all the free spots are filled.
					itemsAddedFromOther.MarkBit(location);
				}
				else
				{
					// already there...if not added from other, mark for remove.
					// *NOTE* Even though BitHelper will check that location is in range, we want
					// to check here. There's no point in checking items beyond originalLastIndex
					// because they could not have been in the original collection
					if (location < originalLastIndex && !itemsAddedFromOther.IsMarked(location))
					{
						itemsToRemove.MarkBit(location);
					}
				}
			}

			// if anything marked, remove it
			for (int i = 0; i < originalLastIndex; i++)
			{
				if (itemsToRemove.IsMarked(i))
				{
					Remove(m_slots[i].value);
				}
			}
		}*/

		/// Add if not already in hashset. Returns an out param indicating index where added. This 
		/// is used by SymmetricExcept because it needs to know the following things:
		/// - whether the item was already present in the collection or added from other
		/// - where it's located (if already present, it will get marked for removal, otherwise
		/// marked for keeping)
		private bool AddOrGetLocation(T value, out int32 location)
		{
			Debug.Assert(mBuckets != null, "Buckets is null, callers should have checked");

			int32 hashCode = (int32)InternalGetHashCode(value);
			int32 bucket = hashCode % (int32)mBuckets.Count;
			for (int32 i = mBuckets[hashCode % mBuckets.Count] - 1; i >= 0; i = mSlots[i].mNext)
			{
				if (mSlots[i].mHashCode == hashCode && /*m_comparer.Equals*/(mSlots[i].mValue == value))
				{
					location = i;
					return false; //already present
				}
			}
			int32 index;
			if (mFreeList >= 0)
			{
				index = mFreeList;
				mFreeList = mSlots[index].mNext;
			}
			else
			{
				if (mLastIndex == mSlots.Count)
				{
					IncreaseCapacity();
					// this will change during resize
					bucket = hashCode % (int32)mBuckets.Count;
				}
				index = mLastIndex;
				mLastIndex++;
			}
			mSlots[index].mHashCode = hashCode;
			mSlots[index].mValue = value;
			mSlots[index].mNext = mBuckets[bucket] - 1;
			mBuckets[bucket] = index + 1;
			mCount++;
#if VERSION_HASHSET
			mVersion++;
#endif
			location = index;
			return true;
		}

		/// Determines counts that can be used to determine equality, subset, and superset. This
		/// is only used when other is an IEnumerable and not a HashSet. If other is a HashSet
		/// these properties can be checked faster without use of marking because we can assume 
		/// other has no duplicates.
		/// 
		/// The following count checks are performed by callers:
		/// 1. Equals: checks if unfoundCount = 0 and uniqueFoundCount = m_count; i.e. everything 
		/// in other is in this and everything in this is in other
		/// 2. Subset: checks if unfoundCount >= 0 and uniqueFoundCount = m_count; i.e. other may
		/// have elements not in this and everything in this is in other
		/// 3. Proper subset: checks if unfoundCount > 0 and uniqueFoundCount = m_count; i.e
		/// other must have at least one element not in this and everything in this is in other
		/// 4. Proper superset: checks if unfound count = 0 and uniqueFoundCount strictly less
		/// than m_count; i.e. everything in other was in this and this had at least one element
		/// not contained in other.
		/// 
		/// An earlier implementation used delegates to perform these checks rather than returning
		/// an ElementCount struct; however this was changed due to the perf overhead of delegates.
		/// @param returnIfUnfound Allows us to finish faster for equals and proper superset
		/// because unfoundCount must be 0.
		
		/*private ElementCount CheckUniqueAndUnfoundElements(IEnumerable<T> other, bool returnIfUnfound)
		{
			ElementCount result;

			// need special case in case this has no elements.
			if (m_count == 0)
			{
				int numElementsInOther = 0;
				foreach (T item in other)
				{
					numElementsInOther++;
					// break right away, all we want to know is whether other has 0 or 1 elements
					break;
				}
				result.uniqueCount = 0;
				result.unfoundCount = numElementsInOther;
				return result;
			}


			Debug.Assert((m_buckets != null) && (m_count > 0), "m_buckets was null but count greater than 0");

			int originalLastIndex = m_lastIndex;
			int intArrayLength = BitHelper.ToIntArrayLength(originalLastIndex);

			BitHelper bitHelper;
			if (intArrayLength <= StackAllocThreshold)
			{
				int* bitArrayPtr = stackallocint[intArrayLength];
				bitHelper = new BitHelper(bitArrayPtr, intArrayLength);
			}
			else
			{
				int[] bitArray = new int[intArrayLength];
				bitHelper = new BitHelper(bitArray, intArrayLength);
			}

			// count of items in other not found in this
			int unfoundCount = 0;
			// count of unique items in other found in this
			int uniqueFoundCount = 0;

			foreach (T item in other)
			{
				int index = InternalIndexOf(item);
				if (index >= 0)
				{
					if (!bitHelper.IsMarked(index))
					{
						// item hasn't been seen yet
						bitHelper.MarkBit(index);
						uniqueFoundCount++;
					}
				}
				else
				{
					unfoundCount++;
					if (returnIfUnfound)
					{
						break;
					}
				}
			}

			result.uniqueCount = uniqueFoundCount;
			result.unfoundCount = unfoundCount;
			return result;
		}*/

		/// Copies this to an array. Used for DebugView
		T[] ToArray()
		{
			T[] newArray = new T[Count];
			CopyTo(newArray);
			return newArray;
		}

		/// Internal method used for HashSetEqualityComparer. Compares set1 and set2 according 
		/// to specified comparer.
		/// 
		/// Because items are hashed according to a specific equality comparer, we have to resort
		/// to n^2 search if they're using different equality comparers.
		/*internal static bool HashSetEquals(HashSet<T> set1, HashSet<T> set2, IEqualityComparer<T> comparer)
		{
			// handle null cases first
			if (set1 == null)
			{
				return (set2 == null);
			}
			else if (set2 == null)
			{
				// set1 != null
				return false;
			}

			// all comparers are the same; this is faster
			if (AreEqualityComparersEqual(set1, set2))
			{
				if (set1.Count != set2.Count)
				{
					return false;
				}
				// suffices to check subset
				foreach (T item in set2)
				{
					if (!set1.Contains(item))
					{
						return false;
					}
				}
				return true;
			}
			else
			{  // n^2 search because items are hashed according to their respective ECs
				foreach (T set2Item in set2)
				{
					bool found = false;
					foreach (T set1Item in set1)
					{
						if (comparer.Equals(set2Item, set1Item))
						{
							found = true;
							break;
						}
					}
					if (!found)
					{
						return false;
					}
				}
				return true;
			}
		}*/

		/// Checks if equality comparers are equal. This is used for algorithms that can
		/// speed up if it knows the other item has unique elements. I.e. if they're using 
		/// different equality comparers, then uniqueness assumption between sets break.
		/*private static bool AreEqualityComparersEqual(HashSet<T> set1, HashSet<T> set2)
		{
			return set1.Comparer.Equals(set2.Comparer);
		}*/

		/// Workaround Comparers that throw ArgumentNullException for GetHashCode(null).
		/// @return hash code
		private int InternalGetHashCode(T item)
		{
			if (item == null)
			{
				return 0;
			}
			return item.GetHashCode() & Lower31BitMask;
		}

		private int InternalGetHashCodeAlt<TAltKey>(TAltKey item) where TAltKey : IHashable
		{
			if (item == null)
			{
				return 0;
			}
			return item.GetHashCode() & Lower31BitMask;
		}

		#endregion

		// used for set checking operations (using enumerables) that rely on counting
		struct ElementCount
		{
			public int32 mUniqueCount;
			public int32 mUnfoundCount;
		}

		struct Slot
		{
			public int32 mHashCode;      // Lower 31 bits of hash code, -1 if unused
			public T mValue;
			public int32 mNext;          // Index of next entry, -1 if last
		}

		public struct Enumerator : IEnumerator<T>
		{
			private HashSet<T> mSet;
			private int32 mIndex;
#if VERSION_HASHSET
			private int32 mVersion;
#endif
			private T mCurrent;

			public this(HashSet<T> set)
			{
				this.mSet = set;
				mIndex = 0;
#if VERSION_HASHSET
				mVersion = set.mVersion;
#endif
				mCurrent = default;
			}

#if VERSION_HASHSET
			void CheckVersion()
			{
				if (mVersion != mSet.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

			public void Dispose()
			{
			}

			public bool MoveNext() mut
			{
#if VERSION_HASHSET
				CheckVersion();
#endif
				while (mIndex < mSet.mLastIndex)
				{
					if (mSet.mSlots[mIndex].mHashCode >= 0)
					{
						mCurrent = mSet.mSlots[mIndex].mValue;
						mIndex++;
						return true;
					}
					mIndex++;
				}
				mIndex = mSet.mLastIndex + 1;
				mCurrent = default(T);
				return false;
			}

			public T Current
			{
				get
				{
					return mCurrent;
				}
			}

			public void Remove() mut
			{
				int_cosize curIdx = mIndex - 1;
				mSet.RemoveEntry(mSet.mSlots[curIdx].mHashCode, curIdx);
#if VERSION_HASHSET
				mVersion = mSet.mVersion;
#endif
				mIndex = curIdx;
			}

			public void Reset() mut
			{
#if VERSION_HASHSET
				CheckVersion();
#endif
				mIndex = 0;
				mCurrent = default(T);
			}

			public Result<T> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
