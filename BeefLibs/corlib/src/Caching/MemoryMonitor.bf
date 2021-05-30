// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Caching.Hosting;
using System.Collections;
using System.Threading;

namespace System.Caching
{
	/// MemoryMonitor is the base class for memory monitors.  The MemoryCache has two types of monitors:  PhysicalMemoryMonitor and CacheMemoryMonitor.  The first monitors
	/// the amount of physical memory used on the machine, and helps determine when we should drop cache entries to avoid paging.  The second monitors the amount of memory used by
	/// the cache itself, and helps determine when we should drop cache entries to avoid exceeding the cache's memory limit.
	abstract class MemoryMonitor
	{
		protected const int TERABYTE_SHIFT = 40;
		protected const int64 TERABYTE = 1L << TERABYTE_SHIFT;

		protected const int GIGABYTE_SHIFT = 30;
		protected const int64 GIGABYTE = 1L << GIGABYTE_SHIFT;

		protected const int MEGABYTE_SHIFT = 20;
		protected const int64 MEGABYTE = 1L << MEGABYTE_SHIFT; // 1048576

		protected const int KILOBYTE_SHIFT = 10;
		protected const int64 KILOBYTE = 1L << KILOBYTE_SHIFT; // 1024

		protected const int HISTORY_COUNT = 6;

		protected int _pressureHigh; // high pressure level
		protected int _pressureLow;  // low pressure level - slow growth here

		protected int _i0;
		protected int[] _pressureHist;
		protected int _pressureTotal;

		private static int64 s_totalPhysical;
		private static int64 s_totalVirtual;

		static this()
		{
			Windows.MEMORYSTATUSEX memoryStatusEx = .();
			memoryStatusEx.Init();

			if (Windows.GlobalMemoryStatusEx(&memoryStatusEx) != 0)
			{
				s_totalPhysical = memoryStatusEx.ullTotalPhys;
				s_totalVirtual = memoryStatusEx.ullTotalVirtual;
			}
		}

		public static int64 TotalPhysical
		{
			get { return s_totalPhysical; }
		}

		public static int64 TotalVirtual
		{
			get { return s_totalVirtual; }
		}

		public int PressureLast
		{
			get { return _pressureHist[_i0]; }
		}

		public int PressureHigh
		{
			get { return _pressureHigh; }
		}

		public int PressureLow
		{
			get { return _pressureLow; }
		}

		public bool IsAboveHighPressure() =>
			PressureLast >= PressureHigh;

		protected abstract int GetCurrentPressure();

		public abstract int GetPercentToTrim(DateTime lastTrimTime, int lastTrimPercent);

		protected void InitHistory()
		{
			Runtime.Assert(_pressureHigh > 0);
			Runtime.Assert(_pressureLow > 0);
			Runtime.Assert(_pressureLow <= _pressureHigh);

			int pressure = GetCurrentPressure();
			_pressureHist = new int[HISTORY_COUNT];

			for (int i = 0; i < HISTORY_COUNT; i++)
			{
				_pressureHist[i] = pressure;
				_pressureTotal += pressure;
			}
		}

		/// Get current pressure and update history
		public void Update()
		{
			int pressure = GetCurrentPressure();
			_i0 = (_i0 + 1) % HISTORY_COUNT;
			_pressureTotal -= _pressureHist[_i0];
			_pressureTotal += pressure;
			_pressureHist[_i0] = pressure;
		}
	}

	sealed class CacheMemoryMonitor : MemoryMonitor, IDisposable
	{
		const int64 PRIVATE_BYTES_LIMIT_2GB = 800 * MEGABYTE;
		const int64 PRIVATE_BYTES_LIMIT_3GB = 1800 * MEGABYTE;
		const int64 PRIVATE_BYTES_LIMIT_64BIT = 1L * TERABYTE;
		const int SAMPLE_COUNT = 2;

		private static IMemoryCacheManager s_memoryCacheManager;
		private static int64 s_autoPrivateBytesLimit = -1;
		private static int64 s_effectiveProcessMemoryLimit = -1;

		private MemoryCache _memoryCache;
		private int64[] _cacheSizeSamples;
		private DateTime[] _cacheSizeSampleTimes;
		private int _idx;
		private List<MemoryCacheStore> _sizedRefMultiple;
		private int64 _memoryLimit;

		public int64 MemoryLimit
		{
			get { return _memoryLimit; }
		}

		private this() { } // hide default ctor

		public this(MemoryCache memoryCache, int cacheMemoryLimitMegabytes)
		{
			_memoryCache = memoryCache;
			_cacheSizeSamples = new int64[SAMPLE_COUNT];
			_cacheSizeSampleTimes = new DateTime[SAMPLE_COUNT];

			if (memoryCache.UseMemoryCacheManager)
				InitMemoryCacheManager(); // This magic thing connects us to ObjectCacheHost magically. :/

			InitDisposableMembers(cacheMemoryLimitMegabytes);
		}

		private void InitDisposableMembers(int cacheMemoryLimitMegabytes)
		{
			bool dispose = true;
			let temp = _memoryCache.AllSRefTargets;
			_sizedRefMultiple = new .(temp.GetEnumerator());
			delete temp;
			SetLimit(cacheMemoryLimitMegabytes);
			InitHistory();
			dispose = false;

			if (dispose)
				Dispose();
		}

		/// Auto-generate the private bytes limit:
		/// - On 64bit, the auto value is MIN(60% physical_ram, 1 TB)
		/// - On x86, for 2GB, the auto value is MIN(60% physical_ram, 800 MB)
		/// - On x86, for 3GB, the auto value is MIN(60% physical_ram, 1800 MB)
		///
		/// - If it's not a hosted environment (e.g. console app), the 60% in the above formulas will become 100% because in un-hosted environment we don't launch
		///   other processes such as compiler, etc.
		private static int64 AutoPrivateBytesLimit
		{
			get
			{
				int64 memoryLimit = s_autoPrivateBytesLimit;

				if (memoryLimit == -1)
				{
#if BF_64_BIT
					bool is64bit = true;
#else
					bool is64bit = false;
#endif
					int64 totalPhysical = TotalPhysical;
					int64 totalVirtual = TotalVirtual;

					if (totalPhysical != 0)
					{
						int64 recommendedPrivateByteLimit;

						if (is64bit)
						{
							recommendedPrivateByteLimit = PRIVATE_BYTES_LIMIT_64BIT;
						}
						else
						{
							// Figure out if it's 2GB or 3GB
							recommendedPrivateByteLimit = totalVirtual > 2 * GIGABYTE ? PRIVATE_BYTES_LIMIT_3GB : PRIVATE_BYTES_LIMIT_2GB;
						}

						// use 60% of physical RAM
						int64 usableMemory = totalPhysical * 3 / 5;
						memoryLimit = Math.Min(usableMemory, recommendedPrivateByteLimit);
					}
					else
					{
						// If GlobalMemoryStatusEx fails, we'll use these as our auto-gen private bytes limit
						memoryLimit = is64bit ? PRIVATE_BYTES_LIMIT_64BIT : PRIVATE_BYTES_LIMIT_2GB;
					}

					Interlocked.Exchange(ref s_autoPrivateBytesLimit, memoryLimit);
				}

				return memoryLimit;
			}
		}

		public void Dispose()
		{
			List<MemoryCacheStore> sref = _sizedRefMultiple;

			if (sref != null && Interlocked.CompareExchange(ref _sizedRefMultiple, null, sref) == sref)
			{
				ClearAndDisposeItems!(sref);
				delete sref;
			}

			IMemoryCacheManager memoryCacheManager = s_memoryCacheManager;

			if (memoryCacheManager != null)
				memoryCacheManager.ReleaseCache(_memoryCache);
		}

		public static int64 EffectiveProcessMemoryLimit
		{
			get
			{
				int64 memoryLimit = s_effectiveProcessMemoryLimit;

				if (memoryLimit == -1)
				{
					memoryLimit = AutoPrivateBytesLimit;
					Interlocked.Exchange(ref s_effectiveProcessMemoryLimit, memoryLimit);
				}

				return memoryLimit;
			}
		}

		protected override int GetCurrentPressure()
		{
			// Call GetUpdatedTotalCacheSize to update the total cache size, if there has been a recent Gen 2 Collection.
			// This update must happen, otherwise the CacheManager won't know the total cache size.
			List<MemoryCacheStore> sref = _sizedRefMultiple;
			// increment the index (it's either 1 or 0)
			Runtime.Assert(SAMPLE_COUNT == 2);
			_idx = _idx ^ 1;
			// remember the sample time
			_cacheSizeSampleTimes[_idx] = DateTime.UtcNow;
			// remember the sample value
			_cacheSizeSamples[_idx] = sref.Count * typeof(MemoryCacheStore).Size;
			IMemoryCacheManager memoryCacheManager = s_memoryCacheManager;

			if (memoryCacheManager != null)
				memoryCacheManager.UpdateCacheSize(_cacheSizeSamples[_idx], _memoryCache);

			// if there's no memory limit, then there's nothing more to do
			if (_memoryLimit <= 0)
				return 0;

			int64 cacheSize = _cacheSizeSamples[_idx];

			// use _memoryLimit as an upper bound so that pressure is a percentage (between 0 and 100, inclusive).
			if (cacheSize > _memoryLimit)
				cacheSize = _memoryLimit;

			int result = (int)(cacheSize * 100 / _memoryLimit);
			return result;
		}

		public override int GetPercentToTrim(DateTime lastTrimTime, int lastTrimPercent)
		{
			int percent = 0;

			if (IsAboveHighPressure())
			{
				int64 cacheSize = _cacheSizeSamples[_idx];

				if (cacheSize > _memoryLimit)
					percent = Math.Min(100, (int)((cacheSize - _memoryLimit) * 100L / cacheSize));
			}

			return percent;
		}

		public void SetLimit(int cacheMemoryLimitMegabytes)
		{
			int64 cacheMemoryLimit = cacheMemoryLimitMegabytes;
			cacheMemoryLimit = cacheMemoryLimit << MEGABYTE_SHIFT;
			_memoryLimit = 0;

			// never override what the user specifies as the limit; only call AutoPrivateBytesLimit when the user does
			// not specify one.
			if (cacheMemoryLimit == 0 && _memoryLimit == 0)
			{
				// Zero means we impose a limit
				_memoryLimit = EffectiveProcessMemoryLimit;
			}
			else if (cacheMemoryLimit != 0 && _memoryLimit != 0)
			{
				// Take the min of "cache memory limit" and the host's "process memory limit".
				_memoryLimit = Math.Min(_memoryLimit, cacheMemoryLimit);
			}
			else if (cacheMemoryLimit != 0)
			{
				// _memoryLimit is 0, but "cache memory limit" is non-zero, so use it as the limit
				_memoryLimit = cacheMemoryLimit;
			}

			if (_memoryLimit > 0)
			{
				_pressureHigh = 100;
				_pressureLow = 80;
			}
			else
			{
				_pressureHigh = 99;
				_pressureLow = 97;
			}
		}

		private static void InitMemoryCacheManager()
		{
			if (s_memoryCacheManager == null)
			{
				IMemoryCacheManager memoryCacheManager = null;
				IServiceProvider host = ObjectCache.Host;

				if (host != null)
					memoryCacheManager = host.GetService(typeof(IMemoryCacheManager)) as IMemoryCacheManager;

				if (memoryCacheManager != null)
					Interlocked.CompareExchange(ref s_memoryCacheManager, memoryCacheManager, null);
			}
		}
	}

	/// PhysicalMemoryMonitor monitors the amound of physical memory used on the machine and helps us determine when to drop entries to avoid paging and GC thrashing.
	/// The limit is configurable (see ConfigUtil.cs).
	sealed class PhysicalMemoryMonitor : MemoryMonitor
	{
		const int MIN_TOTAL_MEMORY_TRIM_PERCENT = 10;
		static readonly int64 TARGET_TOTAL_MEMORY_TRIM_INTERVAL_TICKS = 5 * TimeSpan.TicksPerMinute;

		/// Returns the percentage of physical machine memory that can be consumed by an application
		public int64 MemoryLimit
		{
			get { return _pressureHigh; }
		}

		private this() { } // hide default ctor

		public this(int physicalMemoryLimitPercentage)
		{
			/*
			  The chart below shows physical memory in megabytes, and the 1, 3, and 10% values.
			  When we reach "middle" pressure, we begin trimming the cache.

			  RAM     1%      3%      10%
			  -----------------------------
			  128     1.28    3.84    12.8
			  256     2.56    7.68    25.6
			  512     5.12    15.36   51.2
			  1024    10.24   30.72   102.4
			  2048    20.48   61.44   204.8
			  4096    40.96   122.88  409.6
			  8192    81.92   245.76  819.2

			  Low memory notifications from CreateMemoryResourceNotification are calculated as follows
			  (.\base\ntos\mm\initsup.c):
			  
			  MiInitializeMemoryEvents() {
			  ...
			  //
			  // Scale the threshold so on servers the low threshold is
			  // approximately 32MB per 4GB, capping it at 64MB.
			  //
			  
			  MmLowMemoryThreshold = MmPlentyFreePages;
			  
			  if (MmNumberOfPhysicalPages > 0x40000) {
				  MmLowMemoryThreshold = MI_MB_TO_PAGES (32);
				  MmLowMemoryThreshold += ((MmNumberOfPhysicalPages - 0x40000) >> 7);
			  }
			  else if (MmNumberOfPhysicalPages > 0x8000) {
				  MmLowMemoryThreshold += ((MmNumberOfPhysicalPages - 0x8000) >> 5);
			  }
			  
			  if (MmLowMemoryThreshold > MI_MB_TO_PAGES (64)) {
				  MmLowMemoryThreshold = MI_MB_TO_PAGES (64);
			  }
			  ...

			  E.g.

			  RAM(mb) low      %
			  -------------------
			  256	  20	  92%
			  512	  24	  95%
			  768	  28	  96%
			  1024	  32	  97%
			  2048	  40	  98%
			  3072	  48	  98%
			  4096	  56	  99%
			  5120	  64	  99%
			*/

			int64 memory = TotalPhysical;
			Runtime.Assert(memory != 0, "memory != 0");

			if (memory >= 0x0100000000)
			{
				_pressureHigh = 99;
			}
			else if (memory >= 0x0080000000)
			{
				_pressureHigh = 98;
			}
			else if (memory >= 0x0040000000)
			{
				_pressureHigh = 97;
			}
			else if (memory >= 0x0030000000)
			{
				_pressureHigh = 96;
			}
			else
			{
				_pressureHigh = 95;
			}

			_pressureLow = _pressureHigh - 9;
			SetLimit(physicalMemoryLimitPercentage);
			InitHistory();
		}

		protected override int GetCurrentPressure()
		{
			Windows.MEMORYSTATUSEX memoryStatusEx = .();
			memoryStatusEx.Init();

			if (Windows.GlobalMemoryStatusEx(&memoryStatusEx) == 0)
				return 0;

			int memoryLoad = memoryStatusEx.dwMemoryLoad;
			return memoryLoad;
		}

		public override int GetPercentToTrim(DateTime lastTrimTime, int lastTrimPercent)
		{
			int percent = 0;

			if (IsAboveHighPressure())
			{
				// choose percent such that we don't repeat this for ~5 (TARGET_TOTAL_MEMORY_TRIM_INTERVAL) minutes, but
				// keep the percentage between 10 and 50.
				DateTime utcNow = DateTime.UtcNow;
				int64 ticksSinceTrim = utcNow.Subtract(lastTrimTime).Ticks;

				if (ticksSinceTrim > 0)
				{
					percent = Math.Min(50, (int)((lastTrimPercent * TARGET_TOTAL_MEMORY_TRIM_INTERVAL_TICKS) / ticksSinceTrim));
					percent = Math.Max(MIN_TOTAL_MEMORY_TRIM_PERCENT, percent);
				}
			}

			return percent;
		}

		public void SetLimit(int physicalMemoryLimitPercentage)
		{
			if (physicalMemoryLimitPercentage == 0) // use defaults
				return;

			_pressureHigh = Math.Max(3, physicalMemoryLimitPercentage);
			_pressureLow = Math.Max(1, _pressureHigh - 9);
		}
	}
}
