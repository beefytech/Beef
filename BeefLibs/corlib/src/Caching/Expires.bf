// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	enum EntryState : uint8
	{
		NotInCache,
		AddingToCache,
		AddedToCache,
		RemovingFromCache = 4,
		RemovedFromCache = 8,
		Closed = 16
	}

	[Ordered, CRepr]
	struct ExpiresEntry
	{
		public _aUnion u;
		public int _cFree;
		public MemoryCacheEntry _cacheEntry;

		[Union]
		public struct _aUnion
		{
			public DateTime _utcExpires;
			public ExpiresEntryRef _next;
		}
	}

	struct ExpiresEntryRef
	{
		public static readonly ExpiresEntryRef INVALID = ExpiresEntryRef(0, 0);

		private const uint ENTRY_MASK = 255U;
		private const uint PAGE_MASK = 4294967040U;
		private const int PAGE_SHIFT = 8;
		private uint _ref;

		public this(int pageIndex, int entryIndex)
		{
			_ref = (uint)(pageIndex << 8 | (entryIndex & 255));
		}

		public bool Equals(Object value) =>
			value is ExpiresEntryRef && _ref == ((ExpiresEntryRef)value)._ref;

		public static bool operator!=(ExpiresEntryRef r1, ExpiresEntryRef r2) =>
			r1._ref != r2._ref;

		public static bool operator==(ExpiresEntryRef r1, ExpiresEntryRef r2) =>
			r1._ref == r2._ref;

		public int GetHashCode() =>
			(int)_ref;

		public int PageIndex
		{
			get { return (int)(_ref >> 8); }
		}

		public int Index
		{
			get { return (int)(_ref & 255U); }
		}

		public bool IsInvalid
		{
			get { return _ref == 0U; }
		}
	}

	struct ExpiresPage
	{
		public ExpiresEntry[] _entries;
		public int _pageNext;
		public int _pagePrev;
	}

	struct ExpiresPageList
	{
		public int _head;
		public int _tail;
	}

	sealed class ExpiresBucket
	{
		private const int NUM_ENTRIES = 127;
		private const int LENGTH_ENTRIES = 128;
		private const int MIN_PAGES_INCREMENT = 10;
		private const int MAX_PAGES_INCREMENT = 340;
		private const double MIN_LOAD_FACTOR = 0.5;
		private const int COUNTS_LENGTH = 4;

		private static readonly TimeSpan s_COUNT_INTERVAL = TimeSpan(CacheExpires._tsPerBucket.Ticks / 4L);

		private readonly CacheExpires _cacheExpires;
		private readonly uint8 _bucket;

		private ExpiresPage[] _pages;
		private int _cEntriesInUse;
		private int _cPagesInUse;
		private int _cEntriesInFlush;
		private int _minEntriesInUse;
		private ExpiresPageList _freePageList;
		private ExpiresPageList _freeEntryList;
		private bool _blockReduce;
		private DateTime _utcMinExpires;
		private int[] _counts;
		private DateTime _utcLastCountReset;

		private readonly Monitor _lock = new Monitor() ~ delete _;

		public this(CacheExpires cacheExpires, uint8 bucket, DateTime utcNow)
		{
			_cacheExpires = cacheExpires;
			_bucket = bucket;
			_counts = new int[4];
			ResetCounts(utcNow);
			InitZeroPages();
		}

		public ~this()
		{
			DeletePageArrayAndNull!(_pages);
			delete _counts;
		}

		mixin DeletePageArrayAndNull(ExpiresPage[] arr)
		{
			for (var item in arr)
				if (item._entries != null)
					delete item._entries;

			delete arr;
			arr = null;
		}

		mixin DeleteAndNull(var obj)
		{
			delete obj;
			obj = null;
		}

		private void InitZeroPages()
		{
			if (_pages != null)
				DeletePageArrayAndNull!(_pages);

			_minEntriesInUse = -1;
			_freePageList._head = -1;
			_freePageList._tail = -1;
			_freeEntryList._head = -1;
			_freeEntryList._tail = -1;
		}

		private void ResetCounts(DateTime utcNow)
		{
			_utcLastCountReset = utcNow;
			_utcMinExpires = DateTime.MaxValue;

			for (int i = 0; i < _counts.Count; i++)
				_counts[i] = 0;
		}

		private int GetCountIndex(DateTime utcExpires) =>
			Math.Max(0, (int)((utcExpires - _utcLastCountReset).Ticks / ExpiresBucket.s_COUNT_INTERVAL.Ticks));

		private void AddCount(DateTime utcExpires)
		{
			int countIndex = GetCountIndex(utcExpires);

			for (int i = _counts.Count - 1; i >= countIndex; i--)
				_counts[i]++;

			if (utcExpires < _utcMinExpires)
				_utcMinExpires = utcExpires;
		}

		private void RemoveCount(DateTime utcExpires)
		{
			int countIndex = GetCountIndex(utcExpires);

			for (int i = _counts.Count - 1; i >= countIndex; i--)
				_counts[i]--;
		}

		private int GetExpiresCount(DateTime utcExpires)
		{
			if (utcExpires < _utcMinExpires)
				return 0;

			int countIndex = GetCountIndex(utcExpires);

			if (countIndex >= _counts.Count)
				return _cEntriesInUse;

			return _counts[countIndex];
		}

		private void AddToListHead(int pageIndex, ref ExpiresPageList list)
		{
			_pages[pageIndex]._pagePrev = -1;
			_pages[pageIndex]._pageNext = list._head;

			if (list._head != -1)
			{
				_pages[list._head]._pagePrev = pageIndex;
			}
			else
			{
				list._tail = pageIndex;
			}

			list._head = pageIndex;
		}

		private void AddToListTail(int pageIndex, ref ExpiresPageList list)
		{
			_pages[pageIndex]._pageNext = -1;
			_pages[pageIndex]._pagePrev = list._tail;

			if (list._tail != -1)
			{
				_pages[list._tail]._pageNext = pageIndex;
			}
			else
			{
				list._head = pageIndex;
			}

			list._tail = pageIndex;
		}

		private int RemoveFromListHead(ref ExpiresPageList list)
		{
			int head = list._head;
			RemoveFromList(head, ref list);
			return head;
		}

		private void RemoveFromList(int pageIndex, ref ExpiresPageList list)
		{
			if (_pages[pageIndex]._pagePrev != -1)
			{
				_pages[_pages[pageIndex]._pagePrev]._pageNext = _pages[pageIndex]._pageNext;
			}
			else
			{
				list._head = _pages[pageIndex]._pageNext;
			}

			if (_pages[pageIndex]._pageNext != -1)
			{
				_pages[_pages[pageIndex]._pageNext]._pagePrev = _pages[pageIndex]._pagePrev;
			}
			else
			{
				list._tail = _pages[pageIndex]._pagePrev;
			}

			_pages[pageIndex]._pagePrev = -1;
			_pages[pageIndex]._pageNext = -1;
		}

		private void MoveToListHead(int pageIndex, ref ExpiresPageList list)
		{
			if (list._head == pageIndex)
				return;

			RemoveFromList(pageIndex, ref list);
			AddToListHead(pageIndex, ref list);
		}

		private void MoveToListTail(int pageIndex, ref ExpiresPageList list)
		{
			if (list._tail == pageIndex)
				return;

			RemoveFromList(pageIndex, ref list);
			AddToListTail(pageIndex, ref list);
		}

		private void UpdateMinEntries()
		{
			if (_cPagesInUse <= 1)
			{
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

			if (_pages[pageIndex]._entries != null)
				DeleteAndNull!(_pages[pageIndex]._entries);

			_cPagesInUse--;

			if (_cPagesInUse == 0)
			{
				InitZeroPages();
				return;
			}

			UpdateMinEntries();
		}

		private ExpiresEntryRef GetFreeExpiresEntry()
		{
			int head = _freeEntryList._head;
			ExpiresEntry[] entries = _pages[head]._entries;
			int index = entries[0].u._next.Index;
			entries[0].u._next = entries[index].u._next;
			ExpiresEntry[] array = entries;
			int num = 0;
			array[num]._cFree = array[num]._cFree - 1;

			if (entries[0]._cFree == 0)
				RemoveFromList(head, ref _freeEntryList);

			return ExpiresEntryRef(head, index);
		}

		private void AddExpiresEntryToFreeList(ExpiresEntryRef entryRef)
		{
			ExpiresEntry[] entries = _pages[entryRef.PageIndex]._entries;
			int index = entryRef.Index;
			entries[index]._cFree = 0;
			entries[index].u._next = entries[0].u._next;
			entries[0].u._next = entryRef;
			_cEntriesInUse--;
			int pageIndex = entryRef.PageIndex;
			ExpiresEntry[] array = entries;
			int num = 0;
			array[num]._cFree = array[num]._cFree + 1;

			if (entries[0]._cFree == 1)
			{
				AddToListHead(pageIndex, ref _freeEntryList);
				return;
			}

			if (entries[0]._cFree == 127)
				RemovePage(pageIndex);
		}

		private void Expand()
		{
			if (_freePageList._head == -1)
			{
				int currentPageCount = _pages == null ? 0 : _pages.Count;
				int newPageCount = currentPageCount * 2;
				newPageCount = Math.Max(currentPageCount + 10, newPageCount);
				newPageCount = Math.Min(newPageCount, currentPageCount + 340);
				ExpiresPage[] newPageArr = new .[newPageCount];

				for (int i = 0; i < currentPageCount; i++)
					newPageArr[i] = _pages[i];

				for (int j = currentPageCount; j < newPageArr.Count; j++)
				{
					newPageArr[j]._pagePrev = j - 1;
					newPageArr[j]._pageNext = j + 1;
				}

				newPageArr[currentPageCount]._pagePrev = -1;
				newPageArr[newPageArr.Count - 1]._pageNext = -1;
				_freePageList._head = currentPageCount;
				_freePageList._tail = newPageArr.Count - 1;

				if (_pages != null)
					DeletePageArrayAndNull!(_pages);

				_pages = new .[newPageArr.Count];
				newPageArr.CopyTo(_pages, 0);
				DeletePageArrayAndNull!(newPageArr);
			}

			int newHead = RemoveFromListHead(ref _freePageList);
			AddToListHead(newHead, ref _freeEntryList);
			ExpiresEntry[] newEntryArr = new .[128];
			newEntryArr[0]._cFree = 127;

			for (int k = 0; k < newEntryArr.Count - 1; k++)
				newEntryArr[k].u._next = ExpiresEntryRef(newHead, k + 1);

			if (_pages[newHead]._entries != null)
				DeleteAndNull!(_pages[newHead]._entries);

			newEntryArr[newEntryArr.Count - 1].u._next = ExpiresEntryRef.INVALID;
			_pages[newHead]._entries = new .[newEntryArr.Count];
			newEntryArr.CopyTo(_pages[newHead]._entries, 0);
			DeleteAndNull!(newEntryArr);
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

			for (;;)
			{
				int pageNext = _pages[num2]._pageNext;

				if (_pages[num2]._entries[0]._cFree > num)
				{
					MoveToListTail(num2, ref _freeEntryList);
				}
				else
				{
					MoveToListHead(num2, ref _freeEntryList);
				}

				if (num2 == tail)
					break;

				num2 = pageNext;
			}

			while (_freeEntryList._tail != -1)
			{
				ExpiresEntry[] entries = _pages[_freeEntryList._tail]._entries;
				int num3 = _cPagesInUse * 127 - entries[0]._cFree - _cEntriesInUse;

				if (num3 < 127 - entries[0]._cFree)
					break;

				for (int i = 1; i < entries.Count; i++)
				{
					if (entries[i]._cacheEntry != null)
					{
						ExpiresEntryRef freeExpiresEntry = GetFreeExpiresEntry();
						MemoryCacheEntry cacheEntry = entries[i]._cacheEntry;
						cacheEntry.ExpiresEntryReference = freeExpiresEntry;
						ExpiresEntry[] entries2 = _pages[freeExpiresEntry.PageIndex]._entries;
						entries2[freeExpiresEntry.Index] = entries[i];
						ExpiresEntry[] array = entries;
						int num4 = 0;
						array[num4]._cFree = array[num4]._cFree + 1;
					}
				}

				RemovePage(_freeEntryList._tail);
			}
		}

		public void AddCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				if ((cacheEntry.State & (EntryState)3) != EntryState.NotInCache)
				{
					ExpiresEntryRef expiresEntryRef = cacheEntry.ExpiresEntryReference;

					if (cacheEntry.ExpiresBucket == 255 && expiresEntryRef.IsInvalid)
					{
						if (_freeEntryList._head == -1)
							Expand();

						ExpiresEntryRef freeExpiresEntry = GetFreeExpiresEntry();
						cacheEntry.ExpiresBucket = _bucket;
						cacheEntry.ExpiresEntryReference = freeExpiresEntry;
						ExpiresEntry[] entries = _pages[freeExpiresEntry.PageIndex]._entries;
						int index = freeExpiresEntry.Index;
						entries[index]._cacheEntry = cacheEntry;
						entries[index].u._utcExpires = cacheEntry.UtcAbsExp;
						AddCount(cacheEntry.UtcAbsExp);
						_cEntriesInUse++;

						if ((cacheEntry.State & (EntryState)3) == EntryState.NotInCache)
							RemoveCacheEntryNoLock(cacheEntry);
					}
				}
			}
		}

		private void RemoveCacheEntryNoLock(MemoryCacheEntry cacheEntry)
		{
			ExpiresEntryRef expiresEntryRef = cacheEntry.ExpiresEntryReference;

			if (cacheEntry.ExpiresBucket != _bucket || expiresEntryRef.IsInvalid)
				return;

			ExpiresEntry[] entries = _pages[expiresEntryRef.PageIndex]._entries;
			int index = expiresEntryRef.Index;
			RemoveCount(entries[index].u._utcExpires);
			cacheEntry.ExpiresBucket = uint8.MaxValue;
			cacheEntry.ExpiresEntryReference = ExpiresEntryRef.INVALID;
			DeleteAndNull!(entries[index]._cacheEntry);
			AddExpiresEntryToFreeList(expiresEntryRef);

			if (_cEntriesInUse == 0)
				ResetCounts(DateTime.UtcNow);

			Reduce();
		}

		public void RemoveCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
				RemoveCacheEntryNoLock(cacheEntry);
		}

		public void UtcUpdateCacheEntry(MemoryCacheEntry cacheEntry, DateTime utcExpires)
		{
			using (_lock.Enter())
			{
				ExpiresEntryRef expiresEntryRef = cacheEntry.ExpiresEntryReference;

				if (cacheEntry.ExpiresBucket == _bucket && !expiresEntryRef.IsInvalid)
				{
					ExpiresEntry[] entries = _pages[expiresEntryRef.PageIndex]._entries;
					int index = expiresEntryRef.Index;
					RemoveCount(entries[index].u._utcExpires);
					AddCount(utcExpires);
					entries[index].u._utcExpires = utcExpires;
					cacheEntry.UtcAbsExp = utcExpires;
				}
			}
		}

		public int FlushExpiredItems(DateTime utcNow, bool useInsertBlock)
		{
			if (_cEntriesInUse == 0 || GetExpiresCount(utcNow) == 0)
				return 0;

			ExpiresEntryRef expiresEntryRef = ExpiresEntryRef.INVALID;
			int entriesProcessed = 0;

			if (useInsertBlock)
				_cacheExpires.MemoryCacheStore.BlockInsert();

			using (_lock.Enter())
			{
				if (_cEntriesInUse == 0 || GetExpiresCount(utcNow) == 0)
					return 0;

				ResetCounts(utcNow);
				int pagesInUse = _cPagesInUse;

				for (int i = 0; i < _pages.Count; i++)
				{
					ExpiresEntry[] entries = _pages[i]._entries;

					if (entries != null)
					{
						int usedEntries = 127 - entries[0]._cFree;

						for (int j = 1; j < entries.Count; j++)
						{
							MemoryCacheEntry cacheEntry = entries[j]._cacheEntry;

							if (cacheEntry != null)
							{
								if (entries[j].u._utcExpires > utcNow)
								{
									AddCount(entries[j].u._utcExpires);
								}
								else
								{
									cacheEntry.ExpiresBucket = uint8.MaxValue;
									cacheEntry.ExpiresEntryReference = ExpiresEntryRef.INVALID;
									entries[j]._cFree = 1;
									entries[j].u._next = expiresEntryRef;
									expiresEntryRef = ExpiresEntryRef(i, j);
									entriesProcessed++;
									_cEntriesInFlush++;
								}

								usedEntries--;

								if (usedEntries == 0)
									break;
							}
						}
						pagesInUse--;

						if (pagesInUse == 0)
							break;
					}
				}

				if (entriesProcessed == 0)
					return 0;

				_blockReduce = true;
			}

			if (useInsertBlock)
				_cacheExpires.MemoryCacheStore.UnblockInsert();

			MemoryCacheStore memoryCacheStore = _cacheExpires.MemoryCacheStore;
			ExpiresEntryRef entryRef = expiresEntryRef;

			while (!entryRef.IsInvalid)
			{
				ExpiresEntry[] entries = _pages[entryRef.PageIndex]._entries;
				int index = entryRef.Index;
				ExpiresEntryRef next = entries[index].u._next;
				MemoryCacheEntry cacheEntry = entries[index]._cacheEntry;
				entries[index]._cacheEntry = null;
				var item = memoryCacheStore.Remove(cacheEntry.Key, cacheEntry, CacheEntryRemovedReason.Expired);
				DeleteAndNull!(item);
				entryRef = next;
			}

			if (useInsertBlock)
				_cacheExpires.MemoryCacheStore.BlockInsert();

			using (_lock.Enter())
			{
				entryRef = expiresEntryRef;

				while (!entryRef.IsInvalid)
				{
					ExpiresEntry[] entries = _pages[entryRef.PageIndex]._entries;
					int index = entryRef.Index;
					ExpiresEntryRef next = entries[index].u._next;
					_cEntriesInFlush--;
					AddExpiresEntryToFreeList(entryRef);
					entryRef = next;
				}

				_blockReduce = false;
				Reduce();
			}

			if (useInsertBlock)
				_cacheExpires.MemoryCacheStore.UnblockInsert();

			return entriesProcessed;
		}
	}
}
