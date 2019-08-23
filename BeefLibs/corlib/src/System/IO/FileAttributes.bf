using System;

namespace System.IO
{
    // These correspond to Platform.BfpFileFlags
	public enum FileAttributes
	{
        None = 0,
		Normal = 1,
		Directory = 2,
		SymLink = 4,
		Device = 8,
		ReadOnly = 0x10,
		Hidden = 0x20,
		System = 0x40,
		Temporary = 0x80,
		Offline = 0x100,
		Encrypted = 0x200,
		Archive = 0x400,
    }
}
