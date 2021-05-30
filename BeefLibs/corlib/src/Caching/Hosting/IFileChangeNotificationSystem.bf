// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Caching.Hosting
{
	public interface IFileChangeNotificationSystem {
	    void StartMonitoring(String filePath, OnChangedCallback onChangedCallback, out Object state, out DateTimeOffset lastWriteTime, out int64 fileSize);

	    void StopMonitoring(String filePath, Object state);
	}
}
