using System.Collections;
using System.Diagnostics;

namespace System.IO
{
	public static class Directory
	{
		public static bool Exists(StringView fileName)
		{
			return Platform.BfpDirectory_Exists(fileName.ToScopeCStr!());
		}

		public static Result<void, Platform.BfpFileResult> CreateDirectory(StringView fullPath)
		{
			for (int32 pass = 0; pass < 2; pass++)
			{
				Platform.BfpFileResult result = default;
				Platform.BfpDirectory_Create(fullPath.ToScopeCStr!(), &result);

				if (result == .Ok)
					break;
				
				if (result == .AlreadyExists)
					return .Ok;
				if ((pass == 0) && (result == .NotFound))
				{
					int32 prevSlash = Math.Max((int32)fullPath.LastIndexOf('/'), (int32)fullPath.LastIndexOf('\\'));
					if (prevSlash != -1)
					{
						StringView prevDir = StringView(fullPath, 0, prevSlash);
						Try!(CreateDirectory(prevDir));
						continue;
					}
				}
				return .Err(result);
			}
			return .Ok;
		}
		
		public static Result<void, Platform.BfpFileResult> Delete(StringView path)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpDirectory_Delete(path.ToScopeCStr!(), &result);
			if ((result != .Ok) && (result != .NotFound))
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> DelTree(StringView path)
		{
			if (path.Length <= 2)
				return .Err(.InvalidParameter);
			if ((path[0] != '/') && (path[0] != '\\'))
			{
				if (path[1] == ':')
				{
					if (path.Length < 3)
						return .Err(.InvalidParameter);
				}
			}

		    for (var fileEntry in Directory.EnumerateDirectories(path))
		    {
				let fileName = scope String();
				fileEntry.GetFilePath(fileName);
		        Try!(DelTree(fileName));
		    }

		    for (var fileEntry in Directory.EnumerateFiles(path))
		    {
				let fileName = scope String();
				fileEntry.GetFilePath(fileName);

		        Try!(File.SetAttributes(fileName, FileAttributes.Archive));
		        Try!(File.Delete(fileName));
		    }

			// Allow failure for the directory, this can often be locked for various reasons
			//  but we only consider a file failure to be an "actual" failure
		    Directory.Delete(path).IgnoreError();
			return .Ok;
		}
		
		public static Result<void, Platform.BfpFileResult> Move(StringView oldName, StringView newName)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpDirectory_Rename(oldName.ToScopeCStr!(), newName.ToScopeCStr!(), &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

  		///Copies and overwrites the contents of fromPath into toPath.
		public static Result<void, Platform.BfpFileResult> Copy(StringView fromPath, StringView toPath)
		{
			if(Directory.CreateDirectory(toPath) case .Err(let err))
				return .Err(err);

			for(var file in Directory.EnumerateFiles(fromPath))
				if(File.Copy(file.GetFilePath(.. scope .()), scope $"{toPath}/{file.GetFileName(.. scope .())}") case .Err(let err))
					return .Err(err);

			for(var dir in Directory.EnumerateDirectories(fromPath))
				if(Directory.Copy(dir.GetFilePath(.. scope .()), scope $"{toPath}/{dir.GetFileName(.. scope .())}") case .Err(let err))
					return .Err(err);

			return .Ok;
		}

		public static void GetCurrentDirectory(String outPath)
		{
			Platform.GetStrHelper(outPath, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpDirectory_GetCurrent(outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});
		}

		public static Result<void, Platform.BfpFileResult> SetCurrentDirectory(StringView path)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpDirectory_SetCurrent(path.ToScopeCStr!(), &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		enum EnumerateFlags
		{
			Files = 1,
			Directories = 2
		}

		public static FileEnumerator Enumerate(StringView searchStr, EnumerateFlags flags)
		{
			String useStr = new String(searchStr.Length + 1);
			useStr.Append(searchStr);
			useStr.EnsureNullTerminator();

			Platform.BfpFindFileFlags bfpFlags = .None;
			if (flags.HasFlag(.Directories))
				bfpFlags |= .Directories;
			if (flags.HasFlag(.Files))
				bfpFlags |= .Files;
			let findFileData = Platform.BfpFindFileData_FindFirstFile(useStr, bfpFlags, null);
			return FileEnumerator(useStr, findFileData);
		}

		public static FileEnumerator EnumerateDirectories(StringView dirPath)
		{
			let searchStr = scope String();
			searchStr.Append(dirPath);
			searchStr.Append("/*");
			return Enumerate(searchStr, .Directories);
		}

		public static FileEnumerator EnumerateDirectories(StringView dirPath, StringView wildcard)
		{
			let searchStr = scope String();
			searchStr.Append(dirPath);
			searchStr.Append("/");
			searchStr.Append(wildcard);
			return Enumerate(searchStr, .Directories);
		}

		public static FileEnumerator EnumerateFiles(StringView dirPath)
		{
			let searchStr = scope String();
			searchStr.Append(dirPath);
			searchStr.Append("/*");
			return Enumerate(searchStr, .Files);
		}

		public static FileEnumerator EnumerateFiles(StringView dirPath, StringView wildcard)
		{
			let searchStr = scope String();
			searchStr.Append(dirPath);
			searchStr.Append("/");
			searchStr.Append(wildcard);
			return Enumerate(searchStr, .Files);
		}
	}

	struct FileFindEntry
	{
		String mSearchStr;
		Platform.BfpFindFileData* mFindFileData;

		public this(String searchStr, Platform.BfpFindFileData* findFileData)
		{
			mSearchStr = searchStr;
			mFindFileData = findFileData;
		}

		public bool IsDirectory
		{
			get
			{
				return Platform.BfpFindFileData_GetFileAttributes(mFindFileData).HasFlag(.Directory);
			}
		}

		public void GetFileName(String outFileName)
		{
			Platform.GetStrHelper(outFileName, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpFindFileData_GetFileName(mFindFileData, outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});
		}

		public void GetFilePath(String outPath)
		{
			Path.GetDirectoryPath(mSearchStr, outPath);
			if (!outPath.IsEmpty)
				outPath.Append(Path.DirectorySeparatorChar);
			GetFileName(outPath);
		}

		public DateTime GetLastWriteTime()
		{
			return DateTime.FromFileTime((int64)Platform.BfpFindFileData_GetTime_LastWrite(mFindFileData));
		}

		public DateTime GetLastWriteTimeUtc()
		{
			return DateTime.FromFileTimeUtc((int64)Platform.BfpFindFileData_GetTime_LastWrite(mFindFileData));
		}

		public DateTime GetCreatedTime()
		{
			return DateTime.FromFileTime((int64)Platform.BfpFindFileData_GetTime_Created(mFindFileData));
		}

		public DateTime GetCreatedTimeUtc()
		{
			return DateTime.FromFileTimeUtc((int64)Platform.BfpFindFileData_GetTime_Created(mFindFileData));
		}

		public DateTime GetAccessedTime()
		{
			return DateTime.FromFileTime((int64)Platform.BfpFindFileData_GetTime_Access(mFindFileData));
		}

		public DateTime GetAccessedTimeUtc()
		{
			return DateTime.FromFileTimeUtc((int64)Platform.BfpFindFileData_GetTime_Access(mFindFileData));
		}

		public int64 GetFileSize()
		{
			return Platform.BfpFindFileData_GetFileSize(mFindFileData);
		}

		public Platform.BfpFileAttributes GetFileAttributes()
		{
			return Platform.BfpFindFileData_GetFileAttributes(mFindFileData);
		}
	}

	struct FileEnumerator : IEnumerator<FileFindEntry>, IDisposable
	{
		String mSearchStr;
		Platform.BfpFindFileData* mFindFileData;
		int mIdx;

		public this(String searchStr, Platform.BfpFindFileData* findFileData)
		{
			mSearchStr = searchStr;
			mFindFileData = findFileData;
			mIdx = -1;
		}

		public FileFindEntry Current
		{
			get
			{
				return FileFindEntry(mSearchStr, mFindFileData);
			}
		}

		public void Dispose()
		{
			delete mSearchStr;
			if (mFindFileData != null)
				Platform.BfpFindFileData_Release(mFindFileData);
		}

		public bool MoveNext() mut 
		{
			mIdx++;
			if (mIdx == 0)
				return mFindFileData != null;

			return Platform.BfpFindFileData_FindNextFile(mFindFileData);
		}

		public Result<FileFindEntry> GetNext() mut
		{
			if (!MoveNext())
				return .Err;
			return Current;
		}
	}
}
