// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	[Ordered, CRepr]
	struct UsageEntry
	{
		// Offset 0
		public UsageEntryLink _ref1;
		// Offset 4
		public int _cFree;
		// Offset 8
		public UsageEntryLink _ref2;
		int64 _padding1;
		// Offset 16
		public DateTime _utcDate;
		int64 _padding2;
		// Offset 24
		public MemoryCacheEntry _cacheEntry;
	}

	struct UsagePage
	{
		public UsageEntry[] _entries;
		public int _pageNext;
		public int _pagePrev;
	}

	struct UsagePageList
	{
		public int _head;
		public int _tail;
	}

	struct UsageEntryLink
	{
		public UsageEntryRef _next;
		public UsageEntryRef _prev;
	}

	struct UsageEntryRef
	{
		public static readonly UsageEntryRef INVALID = UsageEntryRef(0, 0);
		private const uint ENTRY_MASK = 255U;
		private const uint PAGE_MASK = 4294967040U;
		private const int PAGE_SHIFT = 8;
		private uint _ref;

		public this(int pageIndex, int entryIndex)
		{
			_ref = (uint)(pageIndex << 8 | (entryIndex & 255));
		}

		public bool Equals(Object value) =>
			value is UsageEntryRef && _ref == ((UsageEntryRef)value)._ref;

		public static bool operator ==(UsageEntryRef r1, UsageEntryRef r2) =>
			r1._ref == r2._ref;

		public static bool operator !=(UsageEntryRef r1, UsageEntryRef r2) =>
			r1._ref != r2._ref;

		public int GetHashCode() =>
			(int)_ref;

		public int PageIndex
		{
			get { return (int)(_ref >> 8); }
		}

		public int Ref1Index
		{
			get { return (int)((int8)(_ref & 255U)); }
		}

		public int Ref2Index
		{
			get { return -(int)((int8)(_ref & 255U)); }
		}

		public bool IsRef1
		{
			get { return (int8)(_ref & 255U) > 0; }
		}

		public bool IsRef2
		{
			get { return (int8)(_ref & 255U) < 0; }
		}

		public bool IsInvalid
		{
			get { return _ref == 0U; }
		}
	}

	sealed class UsageBucket
	{
		private const int NUM_ENTRIES = 127;
		private const int LENGTH_ENTRIES = 128;
		private const int MIN_PAGES_INCREMENT = 10;
		private const int MAX_PAGES_INCREMENT = 340;
		private const double MIN_LOAD_FACTOR = 0.5;

		private CacheUsage _cacheUsage;
		private uint8 _bucket;
		private UsagePage[] _pages;
		private int _cEntriesInUse;
		private int _cPagesInUse;
		private int _cEntriesInFlush;
		private int _minEntriesInUse;
		private UsagePageList _freePageList;
		private UsagePageList _freeEntryList;
		private UsageEntryRef _lastRefHead;
		private UsageEntryRef _lastRefTail;
		private UsageEntryRef _addRef2Head;
		private bool _blockReduce;
		private readonly Monitor _lock = new Monitor() ~ delete _;

		public this(CacheUsage cacheUsage, uint8 bucket)
		{
			_cacheUsage = cacheUsage;
			_bucket = bucket;
			InitZeroPages();
		}

		private void InitZeroPages()
		{
			_pages = null;
			_minEntriesInUse = -1;
			_freePageList._head = -1;
			_freePageList._tail = -1;
			_freeEntryList._head = -1;
			_freeEntryList._tail = -1;
		}

		private void AddToListHead(int pageIndex, ref UsagePageList list)
		{
			_pages[pageIndex]._pagePrev = -1;
			_pages[pageIndex]._pageNext = list._head;

			if (list._head != -1) {
				_pages[list._head]._pagePrev = pageIndex;
			} else {
				list._tail = pageIndex;
			}

			list._head = pageIndex;
		}

		private void AddToListTail(int pageIndex, ref UsagePageList list)
		{
			_pages[pageIndex]._pageNext = -1;
			_pages[pageIndex]._pagePrev = list._tail;

			if (list._tail != -1) {
				_pages[list._tail]._pageNext = pageIndex;
			} else {
				list._head = pageIndex;
			}

			list._tail = pageIndex;
		}

		private int RemoveFromListHead(ref UsagePageList list)
		{
			int head = list._head;
			RemoveFromList(head, ref list);
			return head;
		}

		private void RemoveFromList(int pageIndex, ref UsagePageList list)
		{
			if (_pages[pageIndex]._pagePrev != -1) {
				_pages[_pages[pageIndex]._pagePrev]._pageNext = _pages[pageIndex]._pageNext;
			} else {
				list._head = _pages[pageIndex]._pageNext;
			}

			if (_pages[pageIndex]._pageNext != -1) {
				_pages[_pages[pageIndex]._pageNext]._pagePrev = _pages[pageIndex]._pagePrev;
			} else {
				list._tail = _pages[pageIndex]._pagePrev;
			}

			_pages[pageIndex]._pagePrev = -1;
			_pages[pageIndex]._pageNext = -1;
		}

		private void MoveToListHead(int pageIndex, ref UsagePageList list)
		{
			if (list._head == pageIndex)
				return;

			RemoveFromList(pageIndex, ref list);
			AddToListHead(pageIndex, ref list);
		}

		private void MoveToListTail(int pageIndex, ref UsagePageList list)
		{
			if (list._tail == pageIndex)
				return;

			RemoveFromList(pageIndex, ref list);
			AddToListTail(pageIndex, ref list);
		}

		private void UpdateMinEntries()
		{
			if (_cPagesInUse <= 1) {
				_minEntriesInUse = -1;
				return;
			}

			int num = _cPagesInUse * 127;
			_minEntriesInUse = (int)((double)num * 0.5);

			if (_minEntriesInUse - 1 > (_cPagesInUse - 1) * 127)
				_minEntriesInUse = -1;
		}

		private void RemovePage(int pageIndex)
		{
			RemoveFromList(pageIndex, ref _freeEntryList);
			AddToListHead(pageIndex, ref _freePageList);
			_pages[pageIndex]._entries = null;
			_cPagesInUse--;

			if (_cPagesInUse == 0) {
				InitZeroPages();
				return;
			}

			UpdateMinEntries();
		}

		private UsageEntryRef GetFreeUsageEntry()
		{
			int head = _freeEntryList._head;
			UsageEntry[] entries = _pages[head]._entries;
			int ref1Index = entries[0]._ref1._next.Ref1Index;
			entries[0]._ref1._next = entries[ref1Index]._ref1._next;
			UsageEntry[] array = entries;
			int num = 0;
			array[num]._cFree = array[num]._cFree - 1;

			if (entries[0]._cFree == 0)
				RemoveFromList(head, ref _freeEntryList);

			return UsageEntryRef(head, ref1Index);
		}

		private void AddUsageEntryToFreeList(UsageEntryRef entryRef)
		{
			UsageEntry[] entries = _pages[entryRef.PageIndex]._entries;
			int ref1Index = entryRef.Ref1Index;
			entries[ref1Index]._utcDate = DateTime.MinValue;
			entries[ref1Index]._ref1._prev = UsageEntryRef.INVALID;
			entries[ref1Index]._ref2._next = UsageEntryRef.INVALID;
			entries[ref1Index]._ref2._prev = UsageEntryRef.INVALID;
			entries[ref1Index]._ref1._next = entries[0]._ref1._next;
			entries[0]._ref1._next = entryRef;
			_cEntriesInUse--;
			int pageIndex = entryRef.PageIndex;
			UsageEntry[] array = entries;
			int num = 0;
			array[num]._cFree = array[num]._cFree + 1;

			if (entries[0]._cFree == 1) {
				AddToListHead(pageIndex, ref _freeEntryList);
				return;
			}

			if (entries[0]._cFree == 127)
				RemovePage(pageIndex);
		}

		private void Expand()
		{
			if (_freePageList._head == -1) {
				int num = _pages == null ? 0 : _pages.Count;
				int num2 = num * 2;
				num2 = Math.Max(num + 10, num2);
				num2 = Math.Min(num2, num + 340);
				UsagePage[] array = new UsagePage[num2];

				for (int i = 0; i < num; i++)
					array[i] = _pages[i];

				for (int j = num; j < array.Count; j++) {
					array[j]._pagePrev = j - 1;
					array[j]._pageNext = j + 1;
				}

				array[num]._pagePrev = -1;
				array[array.Count - 1]._pageNext = -1;
				_freePageList._head = num;
				_freePageList._tail = array.Count - 1;
				_pages = array;
			}

			int num3 = RemoveFromListHead(ref _freePageList);
			AddToListHead(num3, ref _freeEntryList);
			UsageEntry[] array2 = new UsageEntry[128];
			array2[0]._cFree = 127;

			for (int k = 0; k < array2.Count - 1; k++)
				array2[k]._ref1._next = UsageEntryRef(num3, k + 1);

			array2[array2.Count - 1]._ref1._next = UsageEntryRef.INVALID;
			_pages[num3]._entries = array2;
			_cPagesInUse++;
			UpdateMinEntries();
		}

		private void Reduce()
		{
			if (_cEntriesInUse >= _minEntriesInUse || _blockReduce)
				return;

			int num = 63;
			int tail = _freeEntryList._tail;
			int num2 = _freeEntryList._head;

			for (;;) {
				int pageNext = _pages[num2]._pageNext;

				if (_pages[num2]._entries[0]._cFree > num) {
					MoveToListTail(num2, ref _freeEntryList);
				} else {
					MoveToListHead(num2, ref _freeEntryList);
				}

				if (num2 == tail)
					break;

				num2 = pageNext;
			}

			while (_freeEntryList._tail != -1) {
				UsageEntry[] entries = _pages[_freeEntryList._tail]._entries;
				int num3 = _cPagesInUse * 127 - entries[0]._cFree - _cEntriesInUse;

				if (num3 < 127 - entries[0]._cFree)
					break;

				for (int i = 1; i < entries.Count; i++) {
					if (entries[i]._cacheEntry != null) {
						UsageEntryRef freeUsageEntry = GetFreeUsageEntry();
						UsageEntryRef usageEntryRef = UsageEntryRef(freeUsageEntry.PageIndex, -freeUsageEntry.Ref1Index);
						UsageEntryRef r = UsageEntryRef(_freeEntryList._tail, i);
						UsageEntryRef r2 = UsageEntryRef(r.PageIndex, -r.Ref1Index);
						MemoryCacheEntry cacheEntry = entries[i]._cacheEntry;
						cacheEntry.UsageEntryReference = freeUsageEntry;
						UsageEntry[] entries2 = _pages[freeUsageEntry.PageIndex]._entries;
						entries2[freeUsageEntry.Ref1Index] = entries[i];
						UsageEntry[] array = entries;
						int num4 = 0;
						array[num4]._cFree = array[num4]._cFree + 1;
						UsageEntryRef r3 = entries2[freeUsageEntry.Ref1Index]._ref1._prev;
						UsageEntryRef r4 = entries2[freeUsageEntry.Ref1Index]._ref1._next;

						if (r4 == r2)
							r4 = usageEntryRef;

						if (r3.IsRef1) {
							_pages[r3.PageIndex]._entries[r3.Ref1Index]._ref1._next = freeUsageEntry;
						} else if (r3.IsRef2) {
							_pages[r3.PageIndex]._entries[r3.Ref2Index]._ref2._next = freeUsageEntry;
						} else {
							_lastRefHead = freeUsageEntry;
						}

						if (r4.IsRef1) {
							_pages[r4.PageIndex]._entries[r4.Ref1Index]._ref1._prev = freeUsageEntry;
						} else if (r4.IsRef2) {
							_pages[r4.PageIndex]._entries[r4.Ref2Index]._ref2._prev = freeUsageEntry;
						} else {
							_lastRefTail = freeUsageEntry;
						}

						r3 = entries2[freeUsageEntry.Ref1Index]._ref2._prev;

						if (r3 == r)
							r3 = freeUsageEntry;

						r4 = entries2[freeUsageEntry.Ref1Index]._ref2._next;

						if (r3.IsRef1) {
							_pages[r3.PageIndex]._entries[r3.Ref1Index]._ref1._next = usageEntryRef;
						} else if (r3.IsRef2) {
							_pages[r3.PageIndex]._entries[r3.Ref2Index]._ref2._next = usageEntryRef;
						} else {
							_lastRefHead = usageEntryRef;
						}

						if (r4.IsRef1) {
							_pages[r4.PageIndex]._entries[r4.Ref1Index]._ref1._prev = usageEntryRef;
						} else if (r4.IsRef2) {
							_pages[r4.PageIndex]._entries[r4.Ref2Index]._ref2._prev = usageEntryRef;
						} else {
							_lastRefTail = usageEntryRef;
						}

						if (_addRef2Head == r2)
							_addRef2Head = usageEntryRef;
					}
				}

				RemovePage(_freeEntryList._tail);
			}
		}

		public void AddCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				if (_freeEntryList._head == -1)
					Expand();

				UsageEntryRef freeUsageEntry = GetFreeUsageEntry();
				UsageEntryRef usageEntryRef = UsageEntryRef(freeUsageEntry.PageIndex, -freeUsageEntry.Ref1Index);
				cacheEntry.UsageEntryReference = freeUsageEntry;
				UsageEntry[] entries = _pages[freeUsageEntry.PageIndex]._entries;
				int ref1Index = freeUsageEntry.Ref1Index;
				entries[ref1Index]._cacheEntry = cacheEntry;
				entries[ref1Index]._utcDate = DateTime.UtcNow;
				entries[ref1Index]._ref1._prev = UsageEntryRef.INVALID;
				entries[ref1Index]._ref2._next = _addRef2Head;

				if (_lastRefHead.IsInvalid) {
					entries[ref1Index]._ref1._next = usageEntryRef;
					entries[ref1Index]._ref2._prev = freeUsageEntry;
					_lastRefTail = usageEntryRef;
				} else {
					entries[ref1Index]._ref1._next = _lastRefHead;

					if (_lastRefHead.IsRef1) {
						_pages[_lastRefHead.PageIndex]._entries[_lastRefHead.Ref1Index]._ref1._prev = freeUsageEntry;
					} else if (_lastRefHead.IsRef2) {
						_pages[_lastRefHead.PageIndex]._entries[_lastRefHead.Ref2Index]._ref2._prev = freeUsageEntry;
					} else {
						_lastRefTail = freeUsageEntry;
					}

					UsageEntryRef prev;
					UsageEntryRef usageEntryRef2;

					if (_addRef2Head.IsInvalid) {
						prev = _lastRefTail;
						usageEntryRef2 = UsageEntryRef.INVALID;
					} else {
						prev = _pages[_addRef2Head.PageIndex]._entries[_addRef2Head.Ref2Index]._ref2._prev;
						usageEntryRef2 = _addRef2Head;
					}

					entries[ref1Index]._ref2._prev = prev;

					if (prev.IsRef1) {
						_pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._next = usageEntryRef;
					} else if (prev.IsRef2) {
						_pages[prev.PageIndex]._entries[prev.Ref2Index]._ref2._next = usageEntryRef;
					} else {
						_lastRefHead = usageEntryRef;
					}

					if (usageEntryRef2.IsRef1) {
						_pages[usageEntryRef2.PageIndex]._entries[usageEntryRef2.Ref1Index]._ref1._prev = usageEntryRef;
					} else if (usageEntryRef2.IsRef2) {
						_pages[usageEntryRef2.PageIndex]._entries[usageEntryRef2.Ref2Index]._ref2._prev = usageEntryRef;
					} else {
						_lastRefTail = usageEntryRef;
					}
				}

				_lastRefHead = freeUsageEntry;
				_addRef2Head = usageEntryRef;
				_cEntriesInUse++;
			}
		}

		private void RemoveEntryFromLastRefList(UsageEntryRef entryRef)
		{
			UsageEntry[] entries = _pages[entryRef.PageIndex]._entries;
			int ref1Index = entryRef.Ref1Index;
			UsageEntryRef prev = entries[ref1Index]._ref1._prev;
			UsageEntryRef next = entries[ref1Index]._ref1._next;

			if (prev.IsRef1) {
				_pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._next = next;
			} else if (prev.IsRef2) {
				_pages[prev.PageIndex]._entries[prev.Ref2Index]._ref2._next = next;
			} else {
				_lastRefHead = next;
			}

			if (next.IsRef1) {
				_pages[next.PageIndex]._entries[next.Ref1Index]._ref1._prev = prev;
			} else if (next.IsRef2) {
				_pages[next.PageIndex]._entries[next.Ref2Index]._ref2._prev = prev;
			} else {
				_lastRefTail = prev;
			}

			prev = entries[ref1Index]._ref2._prev;
			next = entries[ref1Index]._ref2._next;
			UsageEntryRef r = UsageEntryRef(entryRef.PageIndex, -entryRef.Ref1Index);

			if (prev.IsRef1) {
				_pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._next = next;
			} else if (prev.IsRef2) {
				_pages[prev.PageIndex]._entries[prev.Ref2Index]._ref2._next = next;
			} else {
				_lastRefHead = next;
			}

			if (next.IsRef1) {
				_pages[next.PageIndex]._entries[next.Ref1Index]._ref1._prev = prev;
			} else if (next.IsRef2) {
				_pages[next.PageIndex]._entries[next.Ref2Index]._ref2._prev = prev;
			} else {
				_lastRefTail = prev;
			}

			if (_addRef2Head == r)
				_addRef2Head = next;
		}

		public void RemoveCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				UsageEntryRef usageEntryRef = cacheEntry.UsageEntryReference;

				if (!usageEntryRef.IsInvalid) {
					UsageEntry[] entries = _pages[usageEntryRef.PageIndex]._entries;
					int ref1Index = usageEntryRef.Ref1Index;
					cacheEntry.UsageEntryReference = UsageEntryRef.INVALID;
					entries[ref1Index]._cacheEntry = null;
					RemoveEntryFromLastRefList(usageEntryRef);
					AddUsageEntryToFreeList(usageEntryRef);
					Reduce();
				}
			}
		}

		public void UpdateCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				UsageEntryRef usageEntryRef = cacheEntry.UsageEntryReference;

				if (!usageEntryRef.IsInvalid) {
					UsageEntry[] entries = _pages[usageEntryRef.PageIndex]._entries;
					int ref1Index = usageEntryRef.Ref1Index;
					UsageEntryRef usageEntryRef2 = UsageEntryRef(usageEntryRef.PageIndex, -usageEntryRef.Ref1Index);
					UsageEntryRef prev = entries[ref1Index]._ref2._prev;
					UsageEntryRef next = entries[ref1Index]._ref2._next;

					if (prev.IsRef1) {
						_pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._next = next;
					} else if (prev.IsRef2) {
						_pages[prev.PageIndex]._entries[prev.Ref2Index]._ref2._next = next;
					} else {
						_lastRefHead = next;
					}

					if (next.IsRef1) {
						_pages[next.PageIndex]._entries[next.Ref1Index]._ref1._prev = prev;
					} else if (next.IsRef2) {
						_pages[next.PageIndex]._entries[next.Ref2Index]._ref2._prev = prev;
					} else {
						_lastRefTail = prev;
					}

					if (_addRef2Head == usageEntryRef2)
						_addRef2Head = next;

					entries[ref1Index]._ref2 = entries[ref1Index]._ref1;
					prev = entries[ref1Index]._ref2._prev;
					next = entries[ref1Index]._ref2._next;

					if (prev.IsRef1) {
						_pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._next = usageEntryRef2;
					} else if (prev.IsRef2) {
						_pages[prev.PageIndex]._entries[prev.Ref2Index]._ref2._next = usageEntryRef2;
					} else {
						_lastRefHead = usageEntryRef2;
					}

					if (next.IsRef1) {
						_pages[next.PageIndex]._entries[next.Ref1Index]._ref1._prev = usageEntryRef2;
					} else if (next.IsRef2) {
						_pages[next.PageIndex]._entries[next.Ref2Index]._ref2._prev = usageEntryRef2;
					} else {
						_lastRefTail = usageEntryRef2;
					}

					entries[ref1Index]._ref1._prev = UsageEntryRef.INVALID;
					entries[ref1Index]._ref1._next = _lastRefHead;

					if (_lastRefHead.IsRef1) {
						_pages[_lastRefHead.PageIndex]._entries[_lastRefHead.Ref1Index]._ref1._prev = usageEntryRef;
					} else if (_lastRefHead.IsRef2) {
						_pages[_lastRefHead.PageIndex]._entries[_lastRefHead.Ref2Index]._ref2._prev = usageEntryRef;
					} else {
						_lastRefTail = usageEntryRef;
					}

					_lastRefHead = usageEntryRef;
				}
			}
		}

		public int FlushUnderUsedItems(int maxFlush, bool force)
		{
			if (_cEntriesInUse == 0)
				return 0;

			UsageEntryRef usageEntryRef = UsageEntryRef.INVALID;
			int num = 0;
			_cacheUsage.MemoryCacheStore.BlockInsert();
		
			using (_lock.Enter())	
			{
				if (_cEntriesInUse == 0)
					return 0;

				DateTime utcNow = DateTime.UtcNow;
				UsageEntryRef usageEntryRef2 = _lastRefTail;
			
				mixin ProcessEntry(UsageEntry[] entries, int ref2Idx, UsageEntryRef prev)
				{
					UsageEntryRef usageEntryRef3 = UsageEntryRef(usageEntryRef2.PageIndex, usageEntryRef2.Ref2Index);
					MemoryCacheEntry cacheEntry = entries[ref2Idx]._cacheEntry;
					cacheEntry.UsageEntryReference = UsageEntryRef.INVALID;
					RemoveEntryFromLastRefList(usageEntryRef3);
					entries[ref2Idx]._ref1._next = usageEntryRef;
					usageEntryRef = usageEntryRef3;
					num++;
					_cEntriesInFlush++;
					End!(prev);
				}

				mixin End(UsageEntryRef prev)
				{
					usageEntryRef2 = prev;
					continue;
				}

				while (_cEntriesInFlush < maxFlush && !usageEntryRef2.IsInvalid) {
					UsageEntryRef prev = _pages[usageEntryRef2.PageIndex]._entries[usageEntryRef2.Ref2Index]._ref2._prev;

					while (prev.IsRef1) {
						prev = _pages[prev.PageIndex]._entries[prev.Ref1Index]._ref1._prev;
					}

					UsageEntry[] entries = _pages[usageEntryRef2.PageIndex]._entries;
					int ref2Idx = usageEntryRef2.Ref2Index;

					if (force)
						ProcessEntry!(entries, ref2Idx, prev);

					DateTime utcDate = entries[ref2Idx]._utcDate;

					if (!(utcNow - utcDate <= CacheUsage.NEWADD_INTERVAL) || !(utcNow >= utcDate))
						ProcessEntry!(entries, ref2Idx, prev);

					End!(prev);
				}

				if (num == 0)
					return 0;

				_blockReduce = true;
			}

			_cacheUsage.MemoryCacheStore.UnblockInsert();

			MemoryCacheStore memoryCacheStore = _cacheUsage.MemoryCacheStore;
			UsageEntryRef entryRef = usageEntryRef;

			while (!entryRef.IsInvalid) {
				UsageEntry[] entries = _pages[entryRef.PageIndex]._entries;
				int num2 = entryRef.Ref1Index;
				UsageEntryRef next = entries[num2]._ref1._next;
				MemoryCacheEntry cacheEntry = entries[num2]._cacheEntry;
				entries[num2]._cacheEntry = null;
				memoryCacheStore.Remove(cacheEntry.Key, cacheEntry, CacheEntryRemovedReason.Evicted);
				entryRef = next;
			}

			_cacheUsage.MemoryCacheStore.BlockInsert();

			using (_lock.Enter())
			{
				entryRef = usageEntryRef;

				while (!entryRef.IsInvalid) {
					UsageEntry[] entries = _pages[entryRef.PageIndex]._entries;
					int num2 = entryRef.Ref1Index;
					UsageEntryRef next = entries[num2]._ref1._next;
					_cEntriesInFlush--;
					AddUsageEntryToFreeList(entryRef);
					entryRef = next;
				}

				_blockReduce = false;
				Reduce();
			}

			_cacheUsage.MemoryCacheStore.UnblockInsert();
			return num;
		}
	}
}
