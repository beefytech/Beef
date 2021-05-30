// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Caching.Hosting
{
	public interface IMemoryCacheManager
	{
		void UpdateCacheSize(int64 size, MemoryCache cache);
		void ReleaseCache(MemoryCache cache);
	}
}
