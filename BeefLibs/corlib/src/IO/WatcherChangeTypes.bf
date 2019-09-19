namespace System.IO
{
	enum WatcherChangeTypes
	{
		FileCreated = 1,
		DirectoryCreated = 2,
		Deleted = 4,
		Changed = 8,
		Renamed = 0x10,
		Failed = 0x20,
		All = FileCreated | DirectoryCreated | Deleted | Changed | Renamed | Failed
	}
}
