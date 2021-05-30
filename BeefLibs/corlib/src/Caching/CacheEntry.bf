// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections;

namespace System.Caching
{
	public enum CacheEntryRemovedReason
	{
		Removed = 0,           // Explicitly removed via API call
		Expired,
		Evicted,               // Evicted to free up space
		ChangeMonitorChanged,  // An associated programmatic dependency triggered eviction
		CacheSpecificEviction  // Catch-all for custom providers
	}

	public delegate void CacheEntryRemovedCallback(CacheEntryRemovedArguments arguments);

	public delegate void CacheEntryUpdateCallback(CacheEntryUpdateArguments arguments);

	public class CacheEntryRemovedArguments
	{
		private CacheItem _cacheItem ~ delete _; // Needs to be deleted here
		private ObjectCache _source;
		private CacheEntryRemovedReason _reason;

		public CacheItem CacheItem
		{
			get { return _cacheItem; }
		}

		public CacheEntryRemovedReason RemovedReason
		{
			get { return _reason; }
		}

		public ObjectCache Source
		{
			get { return _source; }
		}

		public this(ObjectCache source, CacheEntryRemovedReason reason, CacheItem cacheItem)
		{
			Runtime.Assert(source != null && cacheItem != null);
			_source = source;
			_reason = reason;
			_cacheItem = cacheItem;
		}
	}

	public class CacheEntryUpdateArguments
	{
		private String _key;
		private CacheEntryRemovedReason _reason;
		private ObjectCache _source;
		private CacheItem _updatedCacheItem;
		private CacheItemPolicy _updatedCacheItemPolicy;

		public String Key
		{
			get { return _key; }
		}

		public CacheEntryRemovedReason RemovedReason
		{
			get { return _reason; }
		}

		public ObjectCache Source
		{
			get { return _source; }
		}

		public CacheItem UpdatedCacheItem
		{
			get { return _updatedCacheItem; }
			set { _updatedCacheItem = value; }
		}

		public CacheItemPolicy UpdatedCacheItemPolicy
		{
			get { return _updatedCacheItemPolicy; }
			set { _updatedCacheItemPolicy = value; }
		}

		public this(ObjectCache source, CacheEntryRemovedReason reason, String key)
		{
			Runtime.Assert(source != null && key != null);
			_source = source;
			_reason = reason;
			_key = key;
		}
	}

	public abstract class CacheEntryChangeMonitor : ChangeMonitor
	{
		public abstract List<String> CacheKeys { get; }
		public abstract DateTimeOffset LastModified { get; }
	}
}
