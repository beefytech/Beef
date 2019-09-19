namespace System.IO
{
	public enum NotifyFilters
	{
		FileName     = 0x00000001,
		DirectoryName= 0x00000002,
		Attributes   = 0x00000004,
		Size         = 0x00000008,
		LastWrite    = 0x00000010,
		LastAccess   = 0x00000020,
		CreationTime = 0x00000040,
		Security     = 0x00000100,

		All = FileName | DirectoryName | Attributes | Size  | LastWrite | LastAccess | CreationTime | Security
	}
}
