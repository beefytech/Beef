// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections;
using System.Diagnostics;
using System.Globalization;
using System.Threading;

namespace System.Caching
{
	sealed class MemoryCacheStore : IDisposable
	{
		const int INSERT_BLOCK_WAIT = 10000;
		const int MAX_COUNT = Int32.MaxValue / 2;

		private Dictionary<String, MemoryCacheEntry> _entries = new .() ~ DeleteDictionaryAndValues!(_);
		private readonly Monitor _entriesLock = new .() ~ delete _;
		private CacheExpires _expires = new .(this) ~ delete _;
		private CacheUsage _usage = new .(this);
		private uint8 _disposed;
		private WaitEvent _insertBlock = new .(true) ~ delete _;
		private volatile bool _useInsertBlock;
		private MemoryCache _cache;

		public this(MemoryCache cache)
		{
			_cache = cache;
			_expires.EnableExpirationTimer(true);
		}

		private void AddToCache(MemoryCacheEntry entry)
		{
			// add outside of lock
			if (entry == null)
				return;

			if (entry.HasExpiration())
				_expires.Add(entry);

			if (entry.HasUsage() && (!entry.HasExpiration() || entry.UtcAbsExp - DateTime.UtcNow >= CacheUsage.MIN_LIFETIME_FOR_USAGE))
				_usage.Add(entry);

			// One last sanity check to be sure we didn't fall victim to an Add ----
			if (!entry.CompareExchangeState(.AddedToCache, .AddingToCache))
			{
				if (entry.InExpires())
					_expires.Remove(entry);

				if (entry.InUsage())
					_usage.Remove(entry);
			}

			entry.CallNotifyOnChanged();
		}

		private void RemoveFromCache(MemoryCacheEntry entry, CacheEntryRemovedReason reason, bool delayRelease = false)
		{
			// release outside of lock
			if (entry != null)
			{
				if (entry.InExpires())
					_expires.Remove(entry);

				if (entry.InUsage())
					_usage.Remove(entry);

				Runtime.Assert(entry.State == .RemovingFromCache);
				entry.State = .RemovedFromCache;

				if (!delayRelease)
					entry.Release(_cache, reason);
			}
		}

		/// 'updatePerfCounters' defaults to true since this method is called by all Get() operations to update both the performance counters and the sliding expiration. Callers that perform
		/// nested sliding expiration updates (like a MemoryCacheEntry touching its update sentinel) can pass false to prevent these from unintentionally showing up in the perf counters.
		public void UpdateExpAndUsage(MemoryCacheEntry entry)
		{
			if (entry != null)
			{
				if (entry.InUsage() || entry.SlidingExp > TimeSpan.Zero)
				{
					DateTime utcNow = DateTime.UtcNow;
					entry.UpdateSlidingExp(utcNow, _expires);
					entry.UpdateUsage(utcNow, _usage);
				}

				// If this entry has an update sentinel, the sliding expiration is actually associated  with that sentinel, not with this entry. We need to update the sentinel's sliding expiration to
				// keep the sentinel from expiring, which in turn would force a removal of this entry from the cache.
				entry.UpdateSlidingExpForUpdateSentinel();
			}
		}

		private void WaitInsertBlock() =>
			_insertBlock.WaitFor(INSERT_BLOCK_WAIT);

		public CacheUsage Usage
		{
			get { return _usage; }
		}

		public MemoryCacheEntry AddOrGetExisting(String key, MemoryCacheEntry entry)
		{
			if (_useInsertBlock && entry.HasUsage())
				WaitInsertBlock();

			MemoryCacheEntry existingEntry = null;
			MemoryCacheEntry toBeReleasedEntry = null;
			bool added = false;

			using (_entriesLock.Enter())
			{
				if (_disposed == 0)
				{
					if (_entries.ContainsKey(key))
					{
						existingEntry = _entries[key];

						// has it expired?
						if (existingEntry != null && existingEntry.UtcAbsExp <= DateTime.UtcNow)
						{
							toBeReleasedEntry = existingEntry;
							toBeReleasedEntry.State = .RemovingFromCache;
							existingEntry = null;
						}
					}

					// can we add entry to the cache?
					if (existingEntry == null)
					{
						entry.State = .AddingToCache;
						added = true;
						_entries[key] = entry;
					}
				}

				RemoveFromCache(toBeReleasedEntry, .Expired, true);
			}

			if (added)// add outside of lock
				AddToCache(entry);

			// update outside of lock
			UpdateExpAndUsage(existingEntry);

			// Call Release after the new entry has been completely added so that the CacheItemRemovedCallback can take
			// a dependency on the newly inserted item.
			if (toBeReleasedEntry != null)
				toBeReleasedEntry.Release(_cache, .Expired);

			return existingEntry;
		}

		public void BlockInsert()
		{
			_insertBlock.Reset();
			_useInsertBlock = true;
		}

		public void CopyTo(ref Dictionary<String, Object> h)
		{
			using (_entriesLock.Enter())
			{
				if (_disposed == 0)
				{
					for (let e in _entries)
					{
						MemoryCacheEntry entry = e.value;

						if (entry.UtcAbsExp > DateTime.UtcNow)
							h[e.key] = entry.Value;
					}
				}
			}
		}

		public int Count
		{
			get { return _entries.Count; }
		}

		public void Dispose()
		{
			if (Interlocked.Exchange(ref _disposed, 1) == 0)
			{
				// disable CacheExpires timer
				_expires.EnableExpirationTimer(false);
				// build array list of entries
				List<MemoryCacheEntry> entries = new .(_entries.Count);

				using (_entriesLock.Enter())
				{
					for (let e in _entries)
					{
						MemoryCacheEntry entry = e.value;
						entries.Add(entry);
					}

					for (let entry in entries)
					{
						entry.State = .RemovingFromCache;
						_entries.Remove(entry.Key);
					}
				}

				// release entries outside of lock
				for (let entry in entries)
				{
					RemoveFromCache(entry, .CacheSpecificEviction);
					delete entry;
				}

				// MemoryCacheStatistics has been disposed, and therefore nobody should be using _insertBlock except for
				// potential threads in WaitInsertBlock (which won't care if we call Close).
				Runtime.Assert(_useInsertBlock == false);
				delete _insertBlock;
			}
		}

		public MemoryCacheEntry Get(String key)
		{
			MemoryCacheEntry entry = _entries[key];

			// has it expired?
			if (entry != null && entry.UtcAbsExp <= DateTime.UtcNow)
			{
				Remove(key, entry, .Expired);
				delete entry;
				entry = null;
			}

			// update outside of lock
			UpdateExpAndUsage(entry);
			return entry;
		}

		public MemoryCacheEntry Remove(String key, MemoryCacheEntry entryToRemove, CacheEntryRemovedReason reason)
		{
			MemoryCacheEntry entry = null;

			using (_entriesLock.Enter())
			{
				if (_disposed == 0)
				{
					if (_entries.ContainsKey(key))
					{
						// get current entry
						entry = _entries[key];

						// remove if it matches the entry to be removed (but always remove if entryToRemove is null)
						if (entryToRemove == null || entry == entryToRemove)
						{
							if (entry != null)
							{
								entry.State = .RemovingFromCache;
								_entries.Remove(key);
							}
						}
						else
						{
							entry = null;
						}
					}
				}
			}

			// release outside of lock
			RemoveFromCache(entry, reason);
			return entry;
		}

		public void Set(String key, MemoryCacheEntry entry)
		{
			if (_useInsertBlock && entry.HasUsage())
				WaitInsertBlock();

			MemoryCacheEntry existingEntry = null;
			bool added = false;

			using (_entriesLock.Enter())
			{
				if (_disposed == 0)
				{
					existingEntry = _entries[key];

					if (existingEntry != null)
						existingEntry.State = .RemovingFromCache;

					entry.State = .AddingToCache;
					added = true;
					_entries[key] = entry;
				}
			}

			CacheEntryRemovedReason reason = .Removed;

			if (existingEntry != null)
			{
				if (existingEntry.UtcAbsExp <= DateTime.UtcNow)
					reason = .Expired;

				RemoveFromCache(existingEntry, reason, true);
			}

			if (added)
				AddToCache(entry);

			// Call Release after the new entry has been completely added so that the CacheItemRemovedCallback can take
			// a dependency on the newly inserted item.
			if (existingEntry != null)
				existingEntry.Release(_cache, reason);
		}

		public int64 TrimInternal(int percent)
		{
			Runtime.Assert(percent <= 100);

			int count = Count;
			int toTrim = 0;

			// do we need to drop a percentage of entries?
			if (percent > 0)
			{
				toTrim = (int)Math.Ceiling(((int64)count * (int64)percent) / 100D);
				// would this leave us above MAX_COUNT?
				int minTrim = count - MAX_COUNT;

				if (toTrim < minTrim)
					toTrim = minTrim;
			}

			// do we need to trim?
			if (toTrim <= 0 || _disposed == 1)
				return 0;

			int trimmed = 0; // total number of entries trimmed
			int trimmedOrExpired = 0;

			trimmedOrExpired = _expires.FlushExpiredItems(true);

			if (trimmedOrExpired < toTrim)
			{
				trimmed = _usage.FlushUnderUsedItems(toTrim - trimmedOrExpired);
				trimmedOrExpired += trimmed;
			}

			return trimmedOrExpired;
		}

		public void UnblockInsert()
		{
			_useInsertBlock = false;
			_insertBlock.Set();
		}
	}

	class MemoryCacheEntry
	{
		private String _key ~ if (_.IsDynAlloc) delete _;
		private Object _value;
		private DateTime _utcCreated;
		private int _state;
		private DateTime _utcAbsExp;
		private TimeSpan _slidingExp;
		private ExpiresEntryRef _expiresEntryRef;
		private uint8 _expiresBucket;
		private uint8 _usageBucket;
		private UsageEntryRef _usageEntryRef;
		private DateTime _utcLastUpdateUsage;
		private CacheEntryRemovedCallback _callback;
		private MemoryCacheEntry.SeldomUsedFields _fields ~ delete _;
		private readonly Monitor _lock = new .() ~ delete _;

		private class SeldomUsedFields
		{
			public List<ChangeMonitor> _dependencies;
			public Dictionary<MemoryCacheEntryChangeMonitor, MemoryCacheEntryChangeMonitor> _dependents;
			public MemoryCache _cache;
			public (MemoryCacheStore, MemoryCacheEntry) _updateSentinel = default;
		}

		public String Key
		{
			get { return _key; }
		}

		public Object Value
		{
			get { return _value; }
		}

		public bool HasExpiration() =>
			_utcAbsExp < DateTime.MaxValue;

		public DateTime UtcAbsExp
		{
			get { return _utcAbsExp; }
			set { _utcAbsExp = value; }
		}

		public DateTime UtcCreated
		{
			get { return _utcCreated; }
		}

		public ExpiresEntryRef ExpiresEntryReference
		{
			get { return _expiresEntryRef; }
			set { _expiresEntryRef = value; }
		}

		public uint8 ExpiresBucket
		{
			get { return _expiresBucket; }
			set { _expiresBucket = value; }
		}

		public bool InExpires() =>
			!_expiresEntryRef.IsInvalid;

		public TimeSpan SlidingExp
		{
			get { return _slidingExp; }
		}

		public EntryState State
		{
			get { return (EntryState)_state; }
			set { _state = (int)value; }
		}

		public uint8 UsageBucket
		{
			get { return _usageBucket; }
		}

		public UsageEntryRef UsageEntryReference
		{
			get { return _usageEntryRef; }
			set { _usageEntryRef = value; }
		}

		public DateTime UtcLastUpdateUsage
		{
			get { return _utcLastUpdateUsage; }
			set { _utcLastUpdateUsage = value; }
		}

		public this(String key, Object value, DateTimeOffset absExp, TimeSpan slidingExp, CacheItemPriority priority, List<ChangeMonitor> dependencies,
			CacheEntryRemovedCallback removedCallback, MemoryCache cache)
		{
			Runtime.Assert(value != null);

			_utcCreated = DateTime.UtcNow;
			_key = key;
			_value = value;
			_slidingExp = slidingExp;

			if (_slidingExp > TimeSpan.Zero)
			{
				_utcAbsExp = _utcCreated + _slidingExp;
			}
			else
			{
				_utcAbsExp = absExp.UtcDateTime;
			}

			_expiresEntryRef = ExpiresEntryRef.INVALID;
			_expiresBucket = uint8.MaxValue;
			_usageEntryRef = UsageEntryRef.INVALID;

			if (priority == .NotRemovable)
			{
				_usageBucket = uint8.MaxValue;
			}
			else
			{
				_usageBucket = 0;
			}

			_callback = removedCallback;

			if (dependencies != null)
			{
				_fields = new .();
				_fields._dependencies = dependencies;
				_fields._cache = cache;
			}
		}

		public void AddDependent(MemoryCache cache, MemoryCacheEntryChangeMonitor dependent)
		{
			using (_lock.Enter())
			{
				if (State <= .AddedToCache)
				{
					if (_fields == null)
						_fields = new .();

					if (_fields._cache == null)
						_fields._cache = cache;

					if (_fields._dependents == null)
						_fields._dependents = new .();

					_fields._dependents[dependent] = dependent;
				}
			}
		}

		private void CallCacheEntryRemovedCallback(MemoryCache cache, CacheEntryRemovedReason reason)
		{
			if (_callback == null)
				return;

			CacheEntryRemovedArguments arguments = scope .(cache, reason, new .(_key, _value));
			_callback(arguments);
		}

		public void CallNotifyOnChanged()
		{
			if (_fields != null && _fields._dependencies != null)
			{
				for (let changeMonitor in _fields._dependencies)
					changeMonitor.NotifyOnChanged(new => OnDependencyChanged);
			}
		}

		public bool CompareExchangeState(EntryState value, EntryState comparand) =>
			Interlocked.CompareExchange(ref _state, (int)value, (int)comparand) == (int)comparand;

		public void ConfigureUpdateSentinel(MemoryCacheStore sentinelStore, MemoryCacheEntry sentinelEntry)
		{
			using (_lock.Enter())
			{
				if (_fields == null)
					_fields = new .();

				_fields._updateSentinel = (MemoryCacheStore, MemoryCacheEntry)(sentinelStore, sentinelEntry);
			}
		}

		public bool HasUsage() =>
			_usageBucket != uint8.MaxValue;

		public bool InUsage() =>
			!_usageEntryRef.IsInvalid;

		private void OnDependencyChanged(Object state)
		{
			if (State == EntryState.AddedToCache)
				_fields._cache.RemoveEntry(_key, this, .ChangeMonitorChanged);
		}

		public void Release(MemoryCache cache, CacheEntryRemovedReason reason)
		{
			State = EntryState.Closed;
			IEnumerator<MemoryCacheEntryChangeMonitor> keyCollection = null;

			using (_lock.Enter())
			{
				if (_fields != null && _fields._dependents != null && _fields._dependents.Count > 0)
				{
					keyCollection = _fields._dependents.Keys;
					_fields._dependents = null;
				}
			}

			if (keyCollection != null)
			{
				for (MemoryCacheEntryChangeMonitor memoryCacheEntryChangeMonitor in keyCollection)
					if (memoryCacheEntryChangeMonitor != null)
						memoryCacheEntryChangeMonitor.OnCacheEntryReleased();
			}

			CallCacheEntryRemovedCallback(cache, reason);

			if (_fields != null && _fields._dependencies != null)
				for (ChangeMonitor changeMonitor in _fields._dependencies)
					changeMonitor.Dispose();
		}

		public void RemoveDependent(MemoryCacheEntryChangeMonitor dependent)
		{
			using (_lock.Enter())
				if (_fields != null && _fields._dependents != null)
					_fields._dependents.Remove(dependent);
		}

		public void UpdateSlidingExp(DateTime utcNow, CacheExpires expires)
		{
			if (_slidingExp > TimeSpan.Zero)
			{
				DateTime dateTime = utcNow + _slidingExp;

				if (dateTime - _utcAbsExp >= CacheExpires.MIN_UPDATE_DELTA || dateTime < _utcAbsExp)
					expires.UtcUpdate(this, dateTime);
			}
		}

		public void UpdateSlidingExpForUpdateSentinel()
		{
			MemoryCacheEntry.SeldomUsedFields fields = _fields;

			if (fields != null)
				if (fields._updateSentinel != default)
					fields._updateSentinel.0.UpdateExpAndUsage(fields._updateSentinel.1);
		}

		public void UpdateUsage(DateTime utcNow, CacheUsage usage)
		{
			if (InUsage() && _utcLastUpdateUsage < utcNow - CacheUsage.CORRELATED_REQUEST_TIMEOUT)
			{
				_utcLastUpdateUsage = utcNow;
				usage.Update(this);

				if (_fields != null && _fields._dependencies != null)
				{
					for (ChangeMonitor changeMonitor in _fields._dependencies)
					{
						MemoryCacheEntryChangeMonitor memoryCacheEntryChangeMonitor = (MemoryCacheEntryChangeMonitor)changeMonitor;

						if (memoryCacheEntryChangeMonitor != null)
							for (MemoryCacheEntry memoryCacheEntry in memoryCacheEntryChangeMonitor.Dependencies)
							{
								MemoryCacheStore store = memoryCacheEntry._fields._cache.GetStore(memoryCacheEntry.Key);
								memoryCacheEntry.UpdateUsage(utcNow, store.Usage);
							}
					}
				}
			}
		}
	}

	public class MemoryCache : ObjectCache, IEnumerable<(String key, Object value)>, IDisposable
	{
		private const DefaultCacheCapabilities CAPABILITIES = .InMemoryProvider
			| .CacheEntryChangeMonitors
			| .AbsoluteExpirations
			| .SlidingExpirations
			| .CacheEntryUpdateCallback
			| .CacheEntryRemovedCallback;
		private static readonly TimeSpan OneYear = TimeSpan(365, 0, 0, 0, 0);
		private static Monitor s_initLock = new .() ~ delete _;
		private static MemoryCache s_defaultCache;
		private static CacheEntryRemovedCallback s_sentinelRemovedCallback = new => SentinelEntry.OnCacheEntryRemovedCallback;
		private MemoryCacheStore[] _storeRefs;
		private int _storeCount;
		private int _disposed;
		private MemoryCacheStatistics _stats;
		private String _name;
		private bool _configLess;
		private bool _useMemoryCacheManager = true;

		private bool IsDisposed
		{
			get { return (_disposed == 1); }
		}
		public bool ConfigLess
		{
			get { return _configLess; }
		}

		private class SentinelEntry
		{
			private String _key;
			private ChangeMonitor _expensiveObjectDependency;
			private CacheEntryUpdateCallback _updateCallback;

			public this(String key, ChangeMonitor expensiveObjectDependency, CacheEntryUpdateCallback callback)
			{
				_key = key;
				_expensiveObjectDependency = expensiveObjectDependency;
				_updateCallback = callback;
			}

			public String Key
			{
				get { return _key; }
			}

			public ChangeMonitor ExpensiveObjectDependency
			{
				get { return _expensiveObjectDependency; }
			}

			public CacheEntryUpdateCallback CacheEntryUpdateCallback
			{
				get { return _updateCallback; }
			}

			private static bool IsPolicyValid(CacheItemPolicy policy)
			{
				if (policy == null)
					return false;

				// see if any change monitors have changed
				bool hasChanged = false;
				List<ChangeMonitor> changeMonitors = policy.ChangeMonitors;

				if (changeMonitors != null)
				{
					for (let monitor in changeMonitors)
					{
						if (monitor != null && monitor.HasChanged)
						{
							hasChanged = true;
							break;
						}
					}
				}

				// if the monitors haven't changed yet and we have an update callback then the policy is valid
				if (!hasChanged && policy.UpdateCallback != null)
					return true;

				// if the monitors have changed we need to dispose them
				if (hasChanged)
				{
					for (let monitor in changeMonitors)
					{
						if (monitor != null)
							monitor.Dispose();
					}
				}

				return false;
			}

			public static void OnCacheEntryRemovedCallback(CacheEntryRemovedArguments arguments)
			{
				MemoryCache cache = (MemoryCache)arguments.Source;
				SentinelEntry entry = (SentinelEntry)arguments.CacheItem.Value;
				CacheEntryRemovedReason reason = arguments.RemovedReason;

				switch (reason)
				{
				case .Expired:
					break;
				case .ChangeMonitorChanged:
					if (entry.ExpensiveObjectDependency.HasChanged)
						// If the expensiveObject has been removed explicitly by Cache.Remove, return from the
						// SentinelEntry removed callback thus effectively removing the SentinelEntry from the cache.
						return;

					break;
				case .Evicted:
					Runtime.FatalError("Fatal error: Reason should never be CacheEntryRemovedReason.Evicted since the entry was inserted as NotRemovable.");
				default:
						// do nothing if reason is Removed or CacheSpecificEviction
					return;
				}

				// invoke update callback
				CacheEntryUpdateArguments args = scope .(cache, reason, entry.Key);
				entry.CacheEntryUpdateCallback(args);
				Object expensiveObject = (args.UpdatedCacheItem != null) ? args.UpdatedCacheItem.Value : null;
				CacheItemPolicy policy = args.UpdatedCacheItemPolicy;

				// Only update the "expensive" Object if the user returns a new Object, a policy with update callback, and the change monitors haven't changed.
				// (Inserting with change monitors that have already changed will cause recursion.)
				if (expensiveObject != null && IsPolicyValid(policy)) {
					cache.Set(entry.Key, expensiveObject, policy);
				} else {
					cache.Remove(entry.Key);
				}
			}
		}

		public MemoryCacheStore GetStore(String cacheKey)
		{
			int hashCode = cacheKey.GetHashCode();

			if (hashCode < 0)
				hashCode = (hashCode == Int32.MinValue) ? 0 : -hashCode;

			return _storeRefs[hashCode % _storeCount];
		}

		public MemoryCacheStore[] AllSRefTargets
		{
			get
			{
				MemoryCacheStore[] allStores = new .[_storeCount];

				for (int i = 0; i < _storeCount; i++)
					allStores[i] = _storeRefs[i];

				return allStores;
			}
		}

		private void ValidatePolicy(CacheItemPolicy policy)
		{
			Runtime.Assert(policy.AbsoluteExpiration == .InfiniteAbsoluteExpiration || policy.SlidingExpiration == .NoSlidingExpiration);
			Runtime.Assert(policy.SlidingExpiration > .NoSlidingExpiration || OneYear >= policy.SlidingExpiration);
			Runtime.Assert(policy.RemovedCallback == null || policy.UpdateCallback == null);
			Runtime.Assert(policy.Priority >= .Default && policy.Priority <= .NotRemovable);
		}

		/// Amount of memory that can be used before the cache begins to forcibly remove items.
		public int64 CacheMemoryLimit
		{
			get { return _stats.CacheMemoryLimit; }
		}

		public static MemoryCache Default
		{
			get
			{
				if (s_defaultCache == null)
				{
					using (s_initLock.Enter())
					{
						if (s_defaultCache == null)
							s_defaultCache = new .();
					}
				}

				return s_defaultCache;
			}
		}

		public override DefaultCacheCapabilities DefaultCacheCapabilities
		{
			get { return CAPABILITIES; }
		}

		public override String Name
		{
			get { return _name; }
		}

		public bool UseMemoryCacheManager
		{
			get { return _useMemoryCacheManager; }
		}

		/// Percentage of physical memory that can be used before the cache begins to forcibly remove items.
		public int64 PhysicalMemoryLimit
		{
			get { return _stats.PhysicalMemoryLimit; }
		}

		/// The maximum interval of time after which the cache will update its memory statistics.
		public TimeSpan PollingInterval
		{
			get { return _stats.PollingInterval; }
		}

		/// Only used for Default MemoryCache
		private this()
		{
			_name = "Default";
			Init();
		}

		public this(String name)
		{
			Runtime.Assert(!String.IsNullOrWhiteSpace(name) && !name.Equals("Default"));
			_name = name;
			Init();
		}

		public this(String name, bool ignoreConfigSection)
		{
			Runtime.Assert(!String.IsNullOrWhiteSpace(name) && !name.Equals("Default"));
			_name = name;
			_configLess = ignoreConfigSection;
			Init();
		}

		private void Init()
		{
			_storeCount = Platform.ProcessorCount;
			_storeRefs = new .[_storeCount];
			_useMemoryCacheManager = true;

			for (int i = 0; i < _storeCount; i++)
				_storeRefs[i] = new .(this);

			_stats = new .(this);
		}

		private Object AddOrGetExistingInternal(String key, Object value, CacheItemPolicy policy)
		{
			Runtime.Assert(key != null);
			DateTimeOffset absExp = ObjectCache.InfiniteAbsoluteExpiration;
			TimeSpan slidingExp = ObjectCache.NoSlidingExpiration;
			CacheItemPriority priority = .Default;
			List<ChangeMonitor> changeMonitors = null;
			CacheEntryRemovedCallback removedCallback = null;

			if (policy != null)
			{
				ValidatePolicy(policy);
				Runtime.Assert(policy.UpdateCallback == null);

				absExp = policy.AbsoluteExpiration;
				slidingExp = policy.SlidingExpiration;
				priority = policy.Priority;
				changeMonitors = policy.ChangeMonitors;
				removedCallback = policy.RemovedCallback;
			}

			if (IsDisposed)
			{
				if (changeMonitors != null)
					for (ChangeMonitor monitor in changeMonitors)
						if (monitor != null)
							monitor.Dispose();

				return null;
			}

			MemoryCacheEntry entry = GetStore(key).AddOrGetExisting(key, new .(key, value, absExp, slidingExp, priority, changeMonitors, removedCallback, this));
			return (entry != null) ? entry.Value : null;
		}

		public override CacheEntryChangeMonitor CreateCacheEntryChangeMonitor(IEnumerator<String> keys)
		{
			Runtime.Assert(keys != null);
			List<String> keysClone = new .(keys);
			Runtime.Assert(keysClone.Count > 0);

			for (String key in keysClone)
				Runtime.Assert(key != null);

			return new MemoryCacheEntryChangeMonitor(keysClone, this);
		}

		public void Dispose()
		{
			if (Interlocked.Exchange(ref _disposed, 1) == 0)
			{
				// stats must be disposed prior to disposing the stores.
				if (_stats != null)
					_stats.Dispose();

				if (_storeRefs != null)
				{
					for (var storeRef in _storeRefs)
					{
						if (storeRef != null)
							storeRef.Dispose();
					}

					delete _storeRefs;
				}
			}
		}

		private Object GetInternal(String key)
		{
			Runtime.Assert(key != null);
			MemoryCacheEntry entry = GetEntry(key);
			return (entry != null) ? entry.Value : null;
		}

		public MemoryCacheEntry GetEntry(String key)
		{
			if (IsDisposed)
				return null;

			return GetStore(key).Get(key);
		}

		public override IEnumerator<(String key, Object value)> GetEnumerator()
		{
			Dictionary<String, Object> h = new .();

			if (!IsDisposed)
				for (var storeRef in _storeRefs)
					storeRef.CopyTo(ref h);

			return new box h.GetEnumerator();
		}

		public MemoryCacheEntry RemoveEntry(String key, MemoryCacheEntry entry, CacheEntryRemovedReason reason) =>
			GetStore(key).Remove(key, entry, reason);

		public int64 Trim(int percent)
		{
			var percent;

			if (percent > 100)
				percent = 100;

			int64 trimmed = 0;

			if (_disposed == 0)
				for (var storeRef in _storeRefs)
					trimmed += storeRef.TrimInternal(percent);

			return trimmed;
		}

		/// Default indexer property
		public override Object this[String key]
		{
			get { return GetInternal(key); }
			set { Set(key, value, .InfiniteAbsoluteExpiration); }
		}

		/// Existence check for a single item
		public override bool Contains(String key) =>
			GetInternal(key) != null;

		/// Breaking bug in System.RuntimeCaching.MemoryCache.AddOrGetExisting (CacheItem, CacheItemPolicy)
		public override bool Add(CacheItem item, CacheItemPolicy policy)
		{
			CacheItem existingEntry = AddOrGetExisting(item, policy);
			return (existingEntry == null || existingEntry.Value == null);
		}

		public override Object AddOrGetExisting(String key, Object value, DateTimeOffset absoluteExpiration)
		{
			CacheItemPolicy policy = new .();
			policy.AbsoluteExpiration = absoluteExpiration;
			return AddOrGetExistingInternal(key, value, policy);
		}

		public override CacheItem AddOrGetExisting(CacheItem item, CacheItemPolicy policy)
		{
			Runtime.Assert(item != null);
			return new CacheItem(item.Key, AddOrGetExistingInternal(item.Key, item.Value, policy));
		}

		public override Object AddOrGetExisting(String key, Object value, CacheItemPolicy policy) =>
			AddOrGetExistingInternal(key, value, policy);

		public override Object Get(String key) =>
			GetInternal(key);

		public override CacheItem GetCacheItem(String key)
		{
			Object value = GetInternal(key);
			return (value != null) ? new .(key, value) : null;
		}

		public override void Set(String key, Object value, DateTimeOffset absoluteExpiration)
		{
			CacheItemPolicy policy = new .();
			policy.AbsoluteExpiration = absoluteExpiration;
			Set(key, value, policy);
		}

		public override void Set(CacheItem item, CacheItemPolicy policy)
		{
			Runtime.Assert(item != null);
			Set(item.Key, item.Value, policy);
		}

		public override void Set(String key, Object value, CacheItemPolicy policy)
		{
			Runtime.Assert(key != null);
			DateTimeOffset absExp = ObjectCache.InfiniteAbsoluteExpiration;
			TimeSpan slidingExp = ObjectCache.NoSlidingExpiration;
			CacheItemPriority priority = .Default;
			List<ChangeMonitor> changeMonitors = null;
			CacheEntryRemovedCallback removedCallback = null;

			if (policy != null)
			{
				ValidatePolicy(policy);

				if (policy.UpdateCallback != null)
				{
					Set(key, value, policy.ChangeMonitors, policy.AbsoluteExpiration, policy.SlidingExpiration, policy.UpdateCallback);
					return;
				}

				absExp = policy.AbsoluteExpiration;
				slidingExp = policy.SlidingExpiration;
				priority = policy.Priority;
				changeMonitors = policy.ChangeMonitors;
				removedCallback = policy.RemovedCallback;
			}

			if (IsDisposed)
			{
				if (changeMonitors != null)
					for (let monitor in changeMonitors)
						if (monitor != null)
							monitor.Dispose();

				return;
			}

			MemoryCacheStore store = GetStore(key);
			store.Set(key, new .(key, value, absExp, slidingExp, priority, changeMonitors, removedCallback, this));
		}

		// Add a an event that fires *before* an item is evicted from the Cache
		public void Set(String key, Object value, List<ChangeMonitor> changeMonitors, DateTimeOffset absoluteExpiration, TimeSpan slidingExpiration, CacheEntryUpdateCallback onUpdateCallback)
		{
			var changeMonitors;
			Runtime.Assert(key != null
				&& (changeMonitors != null && absoluteExpiration != ObjectCache.InfiniteAbsoluteExpiration && slidingExpiration != ObjectCache.NoSlidingExpiration)
				&& onUpdateCallback != null);

			if (IsDisposed)
			{
				if (changeMonitors != null)
					for (let monitor in changeMonitors)
						if (monitor != null)
							monitor.Dispose();

				return;
			}

			// Insert updatable cache entry
			MemoryCacheEntry cacheEntry = new .(key, value, ObjectCache.InfiniteAbsoluteExpiration, ObjectCache.NoSlidingExpiration, .NotRemovable, null, null, this);
			GetStore(key).Set(key, cacheEntry);

			// Ensure the sentinel depends on its updatable entry
			String[?] cacheKeys = .(key);
			ChangeMonitor expensiveObjectDep = CreateCacheEntryChangeMonitor(cacheKeys.GetEnumerator());

			if (changeMonitors == null)
				changeMonitors = new List<ChangeMonitor>();

			changeMonitors.Add(expensiveObjectDep);

			// Insert sentinel entry for the updatable cache entry
			String sentinelCacheKey = new $"OnUpdateSentinel{key}";
			MemoryCacheStore sentinelStore = GetStore(sentinelCacheKey);
			MemoryCacheEntry sentinelCacheEntry = new .(sentinelCacheKey, new SentinelEntry(key, expensiveObjectDep, onUpdateCallback), absoluteExpiration,
				slidingExpiration, .NotRemovable, changeMonitors, s_sentinelRemovedCallback, this);
			sentinelStore.Set(sentinelCacheKey, sentinelCacheEntry);
			cacheEntry.ConfigureUpdateSentinel(sentinelStore, sentinelCacheEntry);
		}

		public override Object Remove(String key) =>
			Remove(key, .Removed);

		public Object Remove(String key, CacheEntryRemovedReason reason)
		{
			Runtime.Assert(key != null);

			if (IsDisposed)
				return null;

			MemoryCacheEntry entry = RemoveEntry(key, null, reason);
			return (entry != null) ? entry.Value : null;
		}

		public override int64 GetCount()
		{
			int64 count = 0;

			if (!IsDisposed)
				for (var storeRef in _storeRefs)
					count += storeRef.Count;

			return count;
		}

		public int64 GetLastSize() =>
			_stats.GetLastSize();

		public override Dictionary<String, Object> GetValues(List<String> keys)
		{
			Runtime.Assert(keys != null);
			Dictionary<String, Object> values = null;

			if (!IsDisposed)
			{
				for (let key in keys)
				{
					Runtime.Assert(key != null);
					Object value = GetInternal(key);

					if (value != null)
					{
						if (values == null)
							values = new Dictionary<String, Object>();

						values[key] = value;
					}
				}
			}

			return values;
		}
	}

	sealed class MemoryCacheStatistics : IDisposable
	{
		private const int MEMORYSTATUS_INTERVAL_5_SECONDS = 5000;
		private const int MEMORYSTATUS_INTERVAL_30_SECONDS = 30000;
		private int _configCacheMemoryLimitMegabytes;
		private int _configPhysicalMemoryLimitPercentage;
		private int _configPollingInterval;
		private int _inCacheManagerThread;
		private int _disposed;
		private int64 _lastTrimCount;
		private int64 _lastTrimDurationTicks;
		private int _lastTrimPercent;
		private DateTime _lastTrimTime;
		private int _pollingInterval;
		private PeriodicCallback _timer;
		private Monitor _timerLock = new .() ~ delete _;
		private int64 _totalCountBeforeTrim;
		private CacheMemoryMonitor _cacheMemoryMonitor;
		private MemoryCache _memoryCache;
		private PhysicalMemoryMonitor _physicalMemoryMonitor;

		private this() { }

		private void AdjustTimer()
		{
			using (_timerLock.Enter())
			{
				if (_timer != null)
				{
					if (_physicalMemoryMonitor.IsAboveHighPressure() || _cacheMemoryMonitor.IsAboveHighPressure())
					{
						if (_pollingInterval > 5000)
						{
							_pollingInterval = 5000;
							_timer.UpdateInterval(_pollingInterval);
						}
					}
					else if (_cacheMemoryMonitor.PressureLast > _cacheMemoryMonitor.PressureLow / 2 || _physicalMemoryMonitor.PressureLast > _physicalMemoryMonitor.PressureLow / 2)
					{
						int num = Math.Min(_configPollingInterval, 30000);

						if (_pollingInterval != num)
						{
							_pollingInterval = num;
							_timer.UpdateInterval(_pollingInterval);
						}
					}
					else if (_pollingInterval != _configPollingInterval)
					{
						_pollingInterval = _configPollingInterval;
						_timer.UpdateInterval(_pollingInterval);
					}
				}
			}
		}

		private void CacheManagerTimerCallback(PeriodicCallback state) =>
			CacheManagerThread(0);

		public int64 GetLastSize() =>
			(int64)_cacheMemoryMonitor.PressureLast;

		private int GetPercentToTrim() =>
			Math.Max(_physicalMemoryMonitor.GetPercentToTrim(_lastTrimTime, _lastTrimPercent), _cacheMemoryMonitor.GetPercentToTrim(_lastTrimTime, _lastTrimPercent));

		private void SetTrimStats(int64 trimDurationTicks, int64 totalCountBeforeTrim, int64 trimCount)
		{
			_lastTrimDurationTicks = trimDurationTicks;
			_lastTrimTime = DateTime.UtcNow;
			_totalCountBeforeTrim = totalCountBeforeTrim;
			_lastTrimCount = trimCount;
			_lastTrimPercent = (int)(_lastTrimCount * 100L / _totalCountBeforeTrim);
		}

		private void Update()
		{
			_physicalMemoryMonitor.Update();
			_cacheMemoryMonitor.Update();
		}

		public int64 CacheMemoryLimit
		{
			get { return _cacheMemoryMonitor.MemoryLimit; }
		}

		public int64 PhysicalMemoryLimit
		{
			get { return _physicalMemoryMonitor.MemoryLimit; }
		}

		public TimeSpan PollingInterval
		{
			get { return TimeSpan(_configPollingInterval * TimeSpan.TicksPerMillisecond); }
		}

		public this(MemoryCache memoryCache)
		{
			_memoryCache = memoryCache;
			_lastTrimTime = DateTime.MinValue;
			_configPollingInterval = (int)TimeSpan(0, 0, 20).TotalMilliseconds;
			_configCacheMemoryLimitMegabytes = 0;
			_configPhysicalMemoryLimitPercentage = 0;
			_pollingInterval = _configPollingInterval;
			_physicalMemoryMonitor = new .(_configPhysicalMemoryLimitPercentage);
			_cacheMemoryMonitor = new .(_memoryCache, _configCacheMemoryLimitMegabytes);
			_timer = new .(new => CacheManagerTimerCallback, _configPollingInterval);
		}

		public int64 CacheManagerThread(int minPercent)
		{
			if (Interlocked.Exchange(ref _inCacheManagerThread, 1) != 0)
				return 0L;

			int64 result;

			if (_disposed == 1)
			{
				result = 0L;
			}
			else
			{
				Update();
				AdjustTimer();
				int trimAmount = Math.Max(minPercent, GetPercentToTrim());
				int64 count = _memoryCache.GetCount();
				Stopwatch stopwatch = Stopwatch.StartNew();
				int64 trimmed = _memoryCache.Trim(trimAmount);
				stopwatch.Stop();

				if (trimAmount > 0 && trimmed > 0L)
					SetTrimStats(stopwatch.Elapsed.Ticks, count, trimmed);

				result = trimmed;
				delete stopwatch;
			}

			Interlocked.Exchange(ref _inCacheManagerThread, 0);
			return result;
		}

		public void Dispose()
		{
			if (Interlocked.Exchange(ref _disposed, 1) == 0)
			{
				using (_timerLock.Enter())
				{
					PeriodicCallback timer = _timer;

					if (timer != null && Interlocked.CompareExchange(ref _timer, null, timer) == timer)
						timer.Dispose();
				}

				while (_inCacheManagerThread != 0)
					Thread.Sleep(100);

				if (_cacheMemoryMonitor != null)
					_cacheMemoryMonitor.Dispose();
			}
		}
	}

	sealed class MemoryCacheEntryChangeMonitor : CacheEntryChangeMonitor
	{
		private static readonly DateTime s_DATETIME_MINVALUE_UTC = DateTime(0L, DateTimeKind.Utc);
		private const int MAX_CHAR_COUNT_OF_LONG_CONVERTED_TO_HEXADECIMAL_STRING = 16;
		private List<String> _keys;
		private String _uniqueId;
		private DateTimeOffset _lastModified;
		private List<MemoryCacheEntry> _dependencies;

		public override List<String> CacheKeys
		{
			get { return new .(_keys.GetEnumerator()); }
		}

		public override String UniqueId
		{
			get { return _uniqueId; }
		}

		public override DateTimeOffset LastModified
		{
			get { return _lastModified; }
		}

		public List<MemoryCacheEntry> Dependencies
		{
			get { return _dependencies; }
		}

		private this() { }

		public this(List<String> keys, MemoryCache cache)
		{
			_keys = keys;
			InitDisposableMembers(cache);
		}

		private void InitDisposableMembers(MemoryCache cache)
		{
			bool hasChanged = false;
			_dependencies = new .(_keys.Count);
			String uniqueId = scope .();

			if (_keys.Count == 1)
			{
				String text = _keys[0];
				DateTime dateTime = s_DATETIME_MINVALUE_UTC;
				StartMonitoring(cache, cache.GetEntry(text), ref hasChanged, ref dateTime);

				uniqueId.Append(text);
				dateTime.Ticks.ToString(uniqueId, "X", CultureInfo.InvariantCulture);
				_lastModified = dateTime;
			}
			else
			{
				int capacity = 0;

				for (String key in _keys)
					capacity += key.Length + 16;

				String stringBuilder = scope .(capacity);

				for (let key in _keys)
				{
					DateTime utcCreated = s_DATETIME_MINVALUE_UTC;
					StartMonitoring(cache, cache.GetEntry(key), ref hasChanged, ref utcCreated);
					stringBuilder.Append(key);
					utcCreated.Ticks.ToString(stringBuilder, "X", CultureInfo.InvariantCulture);

					if (utcCreated > _lastModified)
						_lastModified = utcCreated;
				}

				uniqueId.Set(stringBuilder);
			}

			_uniqueId = uniqueId;

			if (hasChanged)
				base.OnChanged(null);

			base.InitializationComplete();
		}

		private void StartMonitoring(MemoryCache cache, MemoryCacheEntry entry, ref bool hasChanged, ref DateTime utcCreated)
		{
			if (entry != null)
			{
				entry.AddDependent(cache, this);
				_dependencies.Add(entry);

				if (entry.State != .AddedToCache)
					hasChanged = true;

				utcCreated = entry.UtcCreated;
				return;
			}

			hasChanged = true;
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing && _dependencies != null)
				for (let memoryCacheEntry in _dependencies)
					if (memoryCacheEntry != null)
						memoryCacheEntry.RemoveDependent(this);
		}

		public void OnCacheEntryReleased() =>
			base.OnChanged(null);
	}
}
