// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	[Ordered]
	struct UsageEntry
	{
		public UsageEntryLink _ref1;
		public int FreeCount;
		public UsageEntryLink _ref2;
		public DateTime UtcDate;
		public MemoryCacheEntry CacheEntry;
	}

	struct UsageEntryLink
	{
		public UsageEntryRef Next;
		public UsageEntryRef Previous;
	}

	struct UsagePage
	{
		public UsageEntry[] Entries;
		public int NextPage;
		public int PreviousPage;
	}

	struct UsagePageList
	{
		public int Head;
		public int Tail;
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

		public static bool operator==(UsageEntryRef r1, UsageEntryRef r2) =>
			r1._ref == r2._ref;

		public static bool operator!=(UsageEntryRef r1, UsageEntryRef r2) =>
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
			_freePageList.Head = -1;
			_freePageList.Tail = -1;
			_freeEntryList.Head = -1;
			_freeEntryList.Tail = -1;
		}

		private void AddToListHead(int pageIndex, ref UsagePageList list)
		{
			_pages[pageIndex].PreviousPage = -1;
			_pages[pageIndex].NextPage = list.Head;

			if (list.Head != -1)
			{
				_pages[list.Head].PreviousPage = pageIndex;
			}
			else
			{
				list.Tail = pageIndex;
			}

			list.Head = pageIndex;
		}

		private void RemoveFromList(int pageIndex, ref UsagePageList list)
		{
			if (_pages[pageIndex].PreviousPage != -1)
			{
				_pages[_pages[pageIndex].PreviousPage].NextPage = _pages[pageIndex].NextPage;
			}
			else
			{
				list.Head = _pages[pageIndex].NextPage;
			}

			if (_pages[pageIndex].NextPage != -1)
			{
				_pages[_pages[pageIndex].NextPage].PreviousPage = _pages[pageIndex].PreviousPage;
			}
			else
			{
				list.Tail = _pages[pageIndex].PreviousPage;
			}

			_pages[pageIndex].PreviousPage = -1;
			_pages[pageIndex].NextPage = -1;
		}

		private void UpdateMinEntries()
		{
			if (_cPagesInUse <= 1)
			{
				_minEntriesInUse = -1;
				return;
			}

			_minEntriesInUse = (int)((double)(_cPagesInUse * 127) * 0.5);

			if (_minEntriesInUse - 1 > (_cPagesInUse - 1) * 127)
				_minEntriesInUse = -1;
		}

		private void RemovePage(int pageIndex)
		{
			RemoveFromList(pageIndex, ref _freeEntryList);
			AddToListHead(pageIndex, ref _freePageList);
			_pages[pageIndex].Entries = null;
			_cPagesInUse--;

			if (_cPagesInUse == 0)
			{
				InitZeroPages();
				return;
			}

			UpdateMinEntries();
		}

		private UsageEntryRef GetFreeUsageEntry()
		{
			int head = _freeEntryList.Head;
			UsageEntry[] entries = _pages[head].Entries;
			int ref1Index = entries[0]._ref1.Next.Ref1Index;
			entries[0]._ref1.Next = entries[ref1Index]._ref1.Next;
			entries[0].FreeCount = entries[0].FreeCount - 1;

			if (entries[0].FreeCount == 0)
				RemoveFromList(head, ref _freeEntryList);

			return UsageEntryRef(head, ref1Index);
		}

		private void AddUsageEntryToFreeList(UsageEntryRef entryRef)
		{
			UsageEntry[] entries = _pages[entryRef.PageIndex].Entries;
			int ref1Index = entryRef.Ref1Index;
			entries[ref1Index].UtcDate = DateTime.MinValue;
			entries[ref1Index]._ref1.Previous = UsageEntryRef.INVALID;
			entries[ref1Index]._ref2.Next = UsageEntryRef.INVALID;
			entries[ref1Index]._ref2.Previous = UsageEntryRef.INVALID;
			entries[ref1Index]._ref1.Next = entries[0]._ref1.Next;
			entries[0]._ref1.Next = entryRef;
			_cEntriesInUse--;
			int pageIndex = entryRef.PageIndex;
			entries[0].FreeCount = entries[0].FreeCount + 1;

			if (entries[0].FreeCount == 1)
			{
				AddToListHead(pageIndex, ref _freeEntryList);
				return;
			}

			if (entries[0].FreeCount == 127)
				RemovePage(pageIndex);
		}

		private void Expand()
		{
			if (_freePageList.Head == -1)
			{
				int pageCount = _pages == null ? 0 : _pages.Count;
				int newPageCount = pageCount * 2;
				newPageCount = Math.Max(pageCount + 10, newPageCount);
				newPageCount = Math.Min(newPageCount, pageCount + 340);
				UsagePage[] pages = new UsagePage[newPageCount];

				for (int i = 0; i < pageCount; i++)
					pages[i] = _pages[i];

				for (int i = pageCount; i < pages.Count; i++)
				{
					pages[i].PreviousPage = i - 1;
					pages[i].NextPage = i + 1;
				}

				pages[pageCount].PreviousPage = -1;
				pages[pages.Count - 1].NextPage = -1;
				_freePageList.Head = pageCount;
				_freePageList.Tail = pages.Count - 1;
				_pages = pages;
			}

			int head = _freePageList.Head;
			RemoveFromList(head, ref _freePageList);

			AddToListHead(head, ref _freeEntryList);
			UsageEntry[] entries = new UsageEntry[128];
			entries[0].FreeCount = 127;

			for (int i = 0; i < entries.Count - 1; i++)
				entries[i]._ref1.Next = UsageEntryRef(head, i + 1);

			entries[entries.Count - 1]._ref1.Next = UsageEntryRef.INVALID;
			_pages[head].Entries = entries;
			_cPagesInUse++;
			UpdateMinEntries();
		}

		private void Reduce()
		{
			if (_cEntriesInUse >= _minEntriesInUse || _blockReduce)
				return;

			int tail = _freeEntryList.Tail;
			int pageIndex = _freeEntryList.Head;

			for (;;)
			{
				int pageNext = _pages[pageIndex].NextPage;

				if (_pages[pageIndex].Entries[0].FreeCount > 63)
				{
					if (_freeEntryList.Tail == pageIndex)
						return;

					RemoveFromList(pageIndex, ref _freeEntryList);

					_pages[pageIndex].NextPage = -1;
					_pages[pageIndex].PreviousPage = _freeEntryList.Tail;

					if (_freeEntryList.Tail != -1)
					{
						_pages[_freeEntryList.Tail].NextPage = pageIndex;
					}
					else
					{
						_freeEntryList.Head = pageIndex;
					}

					_freeEntryList.Tail = pageIndex;
				}
				else
				{
					if (_freeEntryList.Head == pageIndex)
						return;

					RemoveFromList(pageIndex, ref _freeEntryList);
					AddToListHead(pageIndex, ref _freeEntryList);
				}

				if (pageIndex == tail)
					break;

				pageIndex = pageNext;
			}

			while (_freeEntryList.Tail != -1)
			{
				UsageEntry[] entries = _pages[_freeEntryList.Tail].Entries;

				if ((_cPagesInUse * 127 - entries[0].FreeCount - _cEntriesInUse) < 127 - entries[0].FreeCount)
					break;

				for (int i = 1; i < entries.Count; i++)
				{
					if (entries[i].CacheEntry != null)
					{
						UsageEntryRef freeUsageEntry = GetFreeUsageEntry();
						UsageEntryRef usageEntryRef = UsageEntryRef(freeUsageEntry.PageIndex, -freeUsageEntry.Ref1Index);
						UsageEntryRef tailRef = UsageEntryRef(_freeEntryList.Tail, i);
						UsageEntryRef negTailRef = UsageEntryRef(tailRef.PageIndex, -tailRef.Ref1Index);

						entries[i].CacheEntry.UsageEntryReference = freeUsageEntry;
						UsageEntry[] entries2 = _pages[freeUsageEntry.PageIndex].Entries;
						entries2[freeUsageEntry.Ref1Index] = entries[i];
						entries[0].FreeCount = entries[0].FreeCount + 1;

						UsageEntryRef newRefPrev = entries2[freeUsageEntry.Ref1Index]._ref1.Previous;
						UsageEntryRef newRefNext = entries2[freeUsageEntry.Ref1Index]._ref1.Next;

						if (newRefNext == negTailRef)
							newRefNext = usageEntryRef;

						if (newRefPrev.IsRef1)
						{
							_pages[newRefPrev.PageIndex].Entries[newRefPrev.Ref1Index]._ref1.Next = freeUsageEntry;
						}
						else if (newRefPrev.IsRef2)
						{
							_pages[newRefPrev.PageIndex].Entries[newRefPrev.Ref2Index]._ref2.Next = freeUsageEntry;
						}
						else
						{
							_lastRefHead = freeUsageEntry;
						}

						if (newRefNext.IsRef1)
						{
							_pages[newRefNext.PageIndex].Entries[newRefNext.Ref1Index]._ref1.Previous = freeUsageEntry;
						}
						else if (newRefNext.IsRef2)
						{
							_pages[newRefNext.PageIndex].Entries[newRefNext.Ref2Index]._ref2.Previous = freeUsageEntry;
						}
						else
						{
							_lastRefTail = freeUsageEntry;
						}

						newRefPrev = entries2[freeUsageEntry.Ref1Index]._ref2.Previous;

						if (newRefPrev == tailRef)
							newRefPrev = freeUsageEntry;

						newRefNext = entries2[freeUsageEntry.Ref1Index]._ref2.Next;

						if (newRefPrev.IsRef1)
						{
							_pages[newRefPrev.PageIndex].Entries[newRefPrev.Ref1Index]._ref1.Next = usageEntryRef;
						}
						else if (newRefPrev.IsRef2)
						{
							_pages[newRefPrev.PageIndex].Entries[newRefPrev.Ref2Index]._ref2.Next = usageEntryRef;
						}
						else
						{
							_lastRefHead = usageEntryRef;
						}

						if (newRefNext.IsRef1)
						{
							_pages[newRefNext.PageIndex].Entries[newRefNext.Ref1Index]._ref1.Previous = usageEntryRef;
						}
						else if (newRefNext.IsRef2)
						{
							_pages[newRefNext.PageIndex].Entries[newRefNext.Ref2Index]._ref2.Previous = usageEntryRef;
						}
						else
						{
							_lastRefTail = usageEntryRef;
						}

						if (_addRef2Head == negTailRef)
							_addRef2Head = usageEntryRef;
					}
				}

				RemovePage(_freeEntryList.Tail);
			}
		}

		public void AddCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				if (_freeEntryList.Head == -1)
					Expand();

				UsageEntryRef freeUsageEntry = GetFreeUsageEntry();
				UsageEntryRef usageEntryRef = UsageEntryRef(freeUsageEntry.PageIndex, -freeUsageEntry.Ref1Index);
				cacheEntry.UsageEntryReference = freeUsageEntry;
				UsageEntry[] entries = _pages[freeUsageEntry.PageIndex].Entries;
				int ref1Index = freeUsageEntry.Ref1Index;
				entries[ref1Index].CacheEntry = cacheEntry;
				entries[ref1Index].UtcDate = DateTime.UtcNow;
				entries[ref1Index]._ref1.Previous = UsageEntryRef.INVALID;
				entries[ref1Index]._ref2.Next = _addRef2Head;

				if (_lastRefHead.IsInvalid)
				{
					entries[ref1Index]._ref1.Next = usageEntryRef;
					entries[ref1Index]._ref2.Previous = freeUsageEntry;
					_lastRefTail = usageEntryRef;
				}
				else
				{
					entries[ref1Index]._ref1.Next = _lastRefHead;

					if (_lastRefHead.IsRef1)
					{
						_pages[_lastRefHead.PageIndex].Entries[_lastRefHead.Ref1Index]._ref1.Previous = freeUsageEntry;
					}
					else if (_lastRefHead.IsRef2)
					{
						_pages[_lastRefHead.PageIndex].Entries[_lastRefHead.Ref2Index]._ref2.Previous = freeUsageEntry;
					}
					else
					{
						_lastRefTail = freeUsageEntry;
					}

					UsageEntryRef prev;
					UsageEntryRef usageEntryRef2;

					if (_addRef2Head.IsInvalid)
					{
						prev = _lastRefTail;
						usageEntryRef2 = UsageEntryRef.INVALID;
					}
					else
					{
						prev = _pages[_addRef2Head.PageIndex].Entries[_addRef2Head.Ref2Index]._ref2.Previous;
						usageEntryRef2 = _addRef2Head;
					}

					entries[ref1Index]._ref2.Previous = prev;

					if (prev.IsRef1)
					{
						_pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Next = usageEntryRef;
					}
					else if (prev.IsRef2)
					{
						_pages[prev.PageIndex].Entries[prev.Ref2Index]._ref2.Next = usageEntryRef;
					}
					else
					{
						_lastRefHead = usageEntryRef;
					}

					if (usageEntryRef2.IsRef1)
					{
						_pages[usageEntryRef2.PageIndex].Entries[usageEntryRef2.Ref1Index]._ref1.Previous = usageEntryRef;
					}
					else if (usageEntryRef2.IsRef2)
					{
						_pages[usageEntryRef2.PageIndex].Entries[usageEntryRef2.Ref2Index]._ref2.Previous = usageEntryRef;
					}
					else
					{
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
			UsageEntry[] entries = _pages[entryRef.PageIndex].Entries;
			int ref1Index = entryRef.Ref1Index;
			UsageEntryRef prev = entries[ref1Index]._ref1.Previous;
			UsageEntryRef next = entries[ref1Index]._ref1.Next;

			if (prev.IsRef1)
			{
				_pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Next = next;
			}
			else if (prev.IsRef2)
			{
				_pages[prev.PageIndex].Entries[prev.Ref2Index]._ref2.Next = next;
			}
			else
			{
				_lastRefHead = next;
			}

			if (next.IsRef1)
			{
				_pages[next.PageIndex].Entries[next.Ref1Index]._ref1.Previous = prev;
			}
			else if (next.IsRef2)
			{
				_pages[next.PageIndex].Entries[next.Ref2Index]._ref2.Previous = prev;
			}
			else
			{
				_lastRefTail = prev;
			}

			prev = entries[ref1Index]._ref2.Previous;
			next = entries[ref1Index]._ref2.Next;
			UsageEntryRef negRef = UsageEntryRef(entryRef.PageIndex, -entryRef.Ref1Index);

			if (prev.IsRef1)
			{
				_pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Next = next;
			}
			else if (prev.IsRef2)
			{
				_pages[prev.PageIndex].Entries[prev.Ref2Index]._ref2.Next = next;
			}
			else
			{
				_lastRefHead = next;
			}

			if (next.IsRef1)
			{
				_pages[next.PageIndex].Entries[next.Ref1Index]._ref1.Previous = prev;
			}
			else if (next.IsRef2)
			{
				_pages[next.PageIndex].Entries[next.Ref2Index]._ref2.Previous = prev;
			}
			else
			{
				_lastRefTail = prev;
			}

			if (_addRef2Head == negRef)
				_addRef2Head = next;
		}

		public void RemoveCacheEntry(MemoryCacheEntry cacheEntry)
		{
			using (_lock.Enter())
			{
				UsageEntryRef usageEntryRef = cacheEntry.UsageEntryReference;

				if (!usageEntryRef.IsInvalid)
				{
					UsageEntry[] entries = _pages[usageEntryRef.PageIndex].Entries;
					int ref1Index = usageEntryRef.Ref1Index;
					cacheEntry.UsageEntryReference = UsageEntryRef.INVALID;
					entries[ref1Index].CacheEntry = null;
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

				if (!usageEntryRef.IsInvalid)
				{
					UsageEntry[] entries = _pages[usageEntryRef.PageIndex].Entries;
					int ref1Index = usageEntryRef.Ref1Index;
					UsageEntryRef usageEntryRef2 = UsageEntryRef(usageEntryRef.PageIndex, -usageEntryRef.Ref1Index);
					UsageEntryRef prev = entries[ref1Index]._ref2.Previous;
					UsageEntryRef next = entries[ref1Index]._ref2.Next;

					if (prev.IsRef1)
					{
						_pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Next = next;
					}
					else if (prev.IsRef2)
					{
						_pages[prev.PageIndex].Entries[prev.Ref2Index]._ref2.Next = next;
					}
					else
					{
						_lastRefHead = next;
					}

					if (next.IsRef1)
					{
						_pages[next.PageIndex].Entries[next.Ref1Index]._ref1.Previous = prev;
					}
					else if (next.IsRef2)
					{
						_pages[next.PageIndex].Entries[next.Ref2Index]._ref2.Previous = prev;
					}
					else
					{
						_lastRefTail = prev;
					}

					if (_addRef2Head == usageEntryRef2)
						_addRef2Head = next;

					entries[ref1Index]._ref2 = entries[ref1Index]._ref1;
					prev = entries[ref1Index]._ref2.Previous;
					next = entries[ref1Index]._ref2.Next;

					if (prev.IsRef1)
					{
						_pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Next = usageEntryRef2;
					}
					else if (prev.IsRef2)
					{
						_pages[prev.PageIndex].Entries[prev.Ref2Index]._ref2.Next = usageEntryRef2;
					}
					else
					{
						_lastRefHead = usageEntryRef2;
					}

					if (next.IsRef1)
					{
						_pages[next.PageIndex].Entries[next.Ref1Index]._ref1.Previous = usageEntryRef2;
					}
					else if (next.IsRef2)
					{
						_pages[next.PageIndex].Entries[next.Ref2Index]._ref2.Previous = usageEntryRef2;
					}
					else
					{
						_lastRefTail = usageEntryRef2;
					}

					entries[ref1Index]._ref1.Previous = UsageEntryRef.INVALID;
					entries[ref1Index]._ref1.Next = _lastRefHead;

					if (_lastRefHead.IsRef1)
					{
						_pages[_lastRefHead.PageIndex].Entries[_lastRefHead.Ref1Index]._ref1.Previous = usageEntryRef;
					}
					else if (_lastRefHead.IsRef2)
					{
						_pages[_lastRefHead.PageIndex].Entries[_lastRefHead.Ref2Index]._ref2.Previous = usageEntryRef;
					}
					else
					{
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
			int flushedItems = 0;
			_cacheUsage.MemoryCacheStore.BlockInsert();

			using (_lock.Enter())
			{
				if (_cEntriesInUse == 0)
					return 0;

				DateTime utcNow = DateTime.UtcNow;
				UsageEntryRef usageEntryRef2 = _lastRefTail;

				mixin ProcessEntry(UsageEntry[] entries, int idx, UsageEntryRef prev)
				{
					UsageEntryRef usageEntryRef3 = UsageEntryRef(usageEntryRef2.PageIndex, usageEntryRef2.Ref2Index);
					MemoryCacheEntry cacheEntry = entries[idx].CacheEntry;
					cacheEntry.UsageEntryReference = UsageEntryRef.INVALID;
					RemoveEntryFromLastRefList(usageEntryRef3);
					entries[idx]._ref1.Next = usageEntryRef;
					usageEntryRef = usageEntryRef3;
					flushedItems++;
					_cEntriesInFlush++;
					usageEntryRef2 = prev;
					continue;
				}

				while (_cEntriesInFlush < maxFlush && !usageEntryRef2.IsInvalid)
				{
					UsageEntryRef prev = _pages[usageEntryRef2.PageIndex].Entries[usageEntryRef2.Ref2Index]._ref2.Previous;

					while (prev.IsRef1)
						prev = _pages[prev.PageIndex].Entries[prev.Ref1Index]._ref1.Previous;

					UsageEntry[] entries = _pages[usageEntryRef2.PageIndex].Entries;
					int ref2Idx = usageEntryRef2.Ref2Index;

					if (force)
						ProcessEntry!(entries, ref2Idx, prev);

					DateTime utcDate = entries[ref2Idx].UtcDate;

					if (!(utcNow - utcDate <= CacheUsage.NEWADD_INTERVAL) || !(utcNow >= utcDate))
						ProcessEntry!(entries, ref2Idx, prev);

					usageEntryRef2 = prev;
					continue;
				}

				if (flushedItems == 0)
					return 0;

				_blockReduce = true;
			}

			_cacheUsage.MemoryCacheStore.UnblockInsert();

			MemoryCacheStore memoryCacheStore = _cacheUsage.MemoryCacheStore;
			UsageEntryRef entryRef = usageEntryRef;

			while (!entryRef.IsInvalid)
			{
				UsageEntry[] entries = _pages[entryRef.PageIndex].Entries;
				int ref1Idx = entryRef.Ref1Index;
				UsageEntryRef next = entries[ref1Idx]._ref1.Next;
				MemoryCacheEntry cacheEntry = entries[ref1Idx].CacheEntry;
				entries[ref1Idx].CacheEntry = null;
				memoryCacheStore.Remove(cacheEntry.Key, cacheEntry, CacheEntryRemovedReason.Evicted);
				entryRef = next;
			}

			_cacheUsage.MemoryCacheStore.BlockInsert();

			using (_lock.Enter())
			{
				entryRef = usageEntryRef;

				while (!entryRef.IsInvalid)
				{
					UsageEntry[] entries = _pages[entryRef.PageIndex].Entries;
					UsageEntryRef next = entries[entryRef.Ref1Index]._ref1.Next;
					_cEntriesInFlush--;
					AddUsageEntryToFreeList(entryRef);
					entryRef = next;
				}

				_blockReduce = false;
				Reduce();
			}

			_cacheUsage.MemoryCacheStore.UnblockInsert();
			return flushedItems;
		}
	}
}
