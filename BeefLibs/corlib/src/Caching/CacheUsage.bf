// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	class CacheUsage
	{
		public static readonly TimeSpan NEWADD_INTERVAL = TimeSpan(0, 0, 10);
		public static readonly TimeSpan CORRELATED_REQUEST_TIMEOUT = TimeSpan(0, 0, 1);
		public static readonly TimeSpan MIN_LIFETIME_FOR_USAGE = CacheUsage.NEWADD_INTERVAL;

		private const uint8 NUMBUCKETS = 1;
		private const int MAX_REMOVE = 1024;

		private readonly MemoryCacheStore _cacheStore;
		readonly UsageBucket[] _buckets;
		private int _inFlush;

		public this(MemoryCacheStore cacheStore)
		{
			_cacheStore = cacheStore;
			_buckets = new UsageBucket[1];
			uint8 b = 0;

			while ((int)b < _buckets.Count) {
				_buckets[(int)b] = new UsageBucket(this, b);
				b += 1;
			}
		}

		public MemoryCacheStore MemoryCacheStore
		{
			get { return _cacheStore; }
		}

		public void Add(MemoryCacheEntry cacheEntry)
		{
			uint8 usageBucket = cacheEntry.UsageBucket;
			_buckets[(int)usageBucket].AddCacheEntry(cacheEntry);
		}

		public void Remove(MemoryCacheEntry cacheEntry)
		{
			uint8 usageBucket = cacheEntry.UsageBucket;

			if (usageBucket != 255)
				_buckets[(int)usageBucket].RemoveCacheEntry(cacheEntry);
		}

		public void Update(MemoryCacheEntry cacheEntry)
		{
			uint8 usageBucket = cacheEntry.UsageBucket;

			if (usageBucket != 255)
				_buckets[(int)usageBucket].UpdateCacheEntry(cacheEntry);
		}

		public int FlushUnderUsedItems(int toFlush)
		{
			int num = 0;

			if (Interlocked.Exchange(ref _inFlush, 1) == 0) {
				for (UsageBucket usageBucket in _buckets) {
					int num2 = usageBucket.FlushUnderUsedItems(toFlush - num, false);
					num += num2;

					if (num >= toFlush)
						break;
				}

				if (num < toFlush) {
					for (UsageBucket usageBucket2 in _buckets) {
						int num3 = usageBucket2.FlushUnderUsedItems(toFlush - num, true);
						num += num3;

						if (num >= toFlush)
							break;
					}
				}

				Interlocked.Exchange(ref _inFlush, 0);
			}

			return num;
		}
	}
}
