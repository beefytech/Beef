// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System;

namespace System.IO
{
	public enum FileMode : int32
	{
        /// Creates a new file. Fails if the file already exists.
		CreateNew = 1,
    
        /// Creates a new file. If the file already exists, it is overwritten.
		Create = 2,
    
        /// Opens an existing file. Fails if the file does not exist.
		Open = 3,
    
        /// Opens the file if it exists. Otherwise, creates a new file.
		OpenOrCreate = 4,
    
        /// Opens an existing file. Once opened, the file is truncated so that its
        /// size is zero bytes. The calling process must open the file with at least
        /// WRITE access. Fails if the file does not exist.
		Truncate = 5,
        
        /// Opens the file if it exists and seeks to the end.  Otherwise, 
        /// creates a new file.
		Append = 6,
	}
}
