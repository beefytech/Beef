// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System;

namespace System.IO
{
    // Contains constants for controlling file sharing options while
    // opening files.  You can specify what access other processes trying
    // to open the same file concurrently can have.
    //
    // Note these values currently match the values for FILE_SHARE_READ,
    // FILE_SHARE_WRITE, and FILE_SHARE_DELETE in winnt.h
    //
	public enum FileShare : int32
	{
        /// No sharing. Any request to open the file (by this process or another
        /// process) will fail until the file is closed.
		None = 0,
    
        /// Allows subsequent opening of the file for reading. If this flag is not
        /// specified, any request to open the file for reading (by this process or
        /// another process) will fail until the file is closed.
		Read = 1,
    
        /// Allows subsequent opening of the file for writing. If this flag is not
        /// specified, any request to open the file for writing (by this process or
        /// another process) will fail until the file is closed.
		Write = 2,
    
        /// Allows subsequent opening of the file for writing or reading. If this flag
        /// is not specified, any request to open the file for writing or reading (by
        /// this process or another process) will fail until the file is closed.
		ReadWrite = 3,

        /// Open the file, but allow someone else to delete the file.
		Delete = 4,

        /// Whether the file handle should be inheritable by child processes.
        /// Note this is not directly supported like this by Win32.
		Inheritable = 0x10,
	}
}
