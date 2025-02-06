#pragma warning disable 168

using System;
using System.IO;
using System.Collections;
using utils;

namespace res;

class FilePack
{
	public class FindFileData
	{
		public enum Flags
		{
			None = 0,
			Files = 1,
			Directories = 2,
		}

		public virtual bool MoveNext() => false;
		public virtual void GetFileName(String outStr) {}
		public virtual void GetFilePath(String outStr) {}

		public virtual Platform.BfpTimeStamp GetWriteTime() => 0;
		public virtual Platform.BfpTimeStamp GetCreatedTime() => 0;
		public virtual Platform.BfpTimeStamp GetAccessTime() => 0;
		public virtual Platform.BfpFileAttributes GetFileAttributes() => 0;
		public virtual int64 GetFileSize() => 0;
	}

	public class FileEntry
	{
		public virtual void GetFilePath(String outFileName) {}
		public virtual void GetFileName(String outFileName) {}
		public virtual Result<Span<uint8>> GetFileData() => .Err;
		public virtual FindFileData CreateFindFileData(StringView wildcard, FindFileData.Flags flags) => null;
		public virtual Result<void> ExtractTo(StringView dest) => .Err;
	}

	public virtual StringView FilePath => default;

	public virtual bool PathPackMatches(StringView path)
	{
		return false;
	}

	public virtual Stream OpenFile(StringView file)
	{
		return null;
	}

	public virtual FileEntry GetFileEntry(StringView path)
	{
		return null;
	}
}

#if BF_DEPENDS_MINIZ
class ZipFilePack : FilePack
{
	public class ZipFindFileData : FindFileData
	{
		public ZipFileEntry mZipFileEntry;
		public int32 mIdx = -1;
		public String mWildcard ~ delete _;
		public Flags mFlags;

		public override bool MoveNext()
		{
			while (true)
			{
				mIdx++;
				if ((mZipFileEntry.mFiles == null) || (mIdx >= mZipFileEntry.mFiles.Count))
					return false;

				var fileName = GetFileName(.. scope .());
				if (Path.WildcareCompare(fileName, mWildcard))
					break;
			}
			return true;
		}

		public override void GetFilePath(String outFileName)
		{
			var fileEntry = mZipFileEntry.mFiles[mIdx];
			fileEntry.mZipEntry.GetFileName(outFileName);
		}

		public override void GetFileName(String outFileName)
		{
			var fileEntry = mZipFileEntry.mFiles[mIdx];
			var filePath = fileEntry.mZipEntry.GetFileName(.. scope .());
			Path.GetFileName(filePath, outFileName);
		}

		public override Platform.BfpTimeStamp GetWriteTime()
		{
			return (.)mZipFileEntry.mFiles[mIdx].mZipEntry.GetTimestamp();
		}

		public override Platform.BfpTimeStamp GetCreatedTime()
		{
			return GetWriteTime();
		}

		public override Platform.BfpTimeStamp GetAccessTime()
		{
			return GetWriteTime();
		}

		public override Platform.BfpFileAttributes GetFileAttributes()
		{
			return .Normal;
		}

		public override int64 GetFileSize()
		{
			return mZipFileEntry.mFiles[mIdx].mZipEntry.GetUncompressedSize();
		}
	}

	public class ZipFileEntry : FileEntry
	{
		public MiniZ.ZipFile.Entry mZipEntry ~ delete _;
		public List<ZipFileEntry> mFiles ~ delete _;

		public override Result<Span<uint8>> GetFileData()
		{
			return mZipEntry.ExtractToMemory();
		}

		public override FindFileData CreateFindFileData(StringView wildcard, FindFileData.Flags flags)
		{
			var findFileData = new ZipFindFileData();
			findFileData.mZipFileEntry = this;
			findFileData.mWildcard = new .(wildcard);
			findFileData.mFlags = flags;
			return findFileData;
		}

		public override Result<void> ExtractTo(StringView dest)
		{
			return mZipEntry.ExtractToFile(dest);
		}

		public override void GetFilePath(String outFileName)
		{
			mZipEntry.GetFileName(outFileName);
		}

		public override void GetFileName(String outFileName)
		{
			var filePath = mZipEntry.GetFileName(.. scope .());
			Path.GetFileName(filePath, outFileName);
		}
	}

	String mPath ~ delete _;
	MiniZ.ZipFile mZipFile ~ delete _;
	Dictionary<String, ZipFileEntry> mFileMap ~ DeleteDictionaryAndKeysAndValues!(_);
	ZipFileEntry mRootFileEntry;

	public List<ZipFileEntry> RootFiles
	{
		get
		{
			FillFileMap();
			return mRootFileEntry.mFiles;
		}
	}

	public this()
	{
		
	}

	public override StringView FilePath => mPath;

	public Result<void> Init(StringView path)
	{
		mPath = new .(path);
		mZipFile = new MiniZ.ZipFile();
		Try!(mZipFile.OpenMapped(path));
		return .Ok;
	}

	public Result<void> Init(StringView path, Range range)
	{
		mPath = new .(path);
		mZipFile = new MiniZ.ZipFile();
		Try!(mZipFile.OpenMapped(path, range));
		return .Ok;
	}

	public Result<void> Init(StringView path, Span<uint8> data)
	{
		mPath = new .(path);
		mZipFile = new MiniZ.ZipFile();
		Try!(mZipFile.Open(data));
		return .Ok;
	}

	void FillFileMap()
	{
		if (mFileMap != null)
			return;
		mRootFileEntry = new .();
		mRootFileEntry.mFiles = new .();
		mFileMap = new .();
		mFileMap[new .("/")] = mRootFileEntry;

		for (int i < mZipFile.GetNumFiles())
		{
			var entry = new MiniZ.ZipFile.Entry();
			mZipFile.SelectEntry(i, entry);
			var fileName = entry.GetFileName(.. new .());

			ZipFileEntry zipFileEntry = new .();
			zipFileEntry.mZipEntry = entry;
			mFileMap[fileName] = zipFileEntry;

			var checkFileName = StringView(fileName);
			if (checkFileName.EndsWith('/'))
				checkFileName.RemoveFromEnd(1);
			var fileDir = Path.GetDirectoryPath(checkFileName, .. scope .());
			fileDir.Append("/");
			if (mFileMap.TryGet(fileDir, ?, var dirEntry))
			{
				if (dirEntry.mFiles == null)
					dirEntry.mFiles = new .();
				dirEntry.mFiles.Add(zipFileEntry);
			}
		}
	}

	public override FileEntry GetFileEntry(StringView path)
	{
		var path;
		FillFileMap();
		if (path.StartsWith('/'))
			path.RemoveFromStart(1);
		if (path.Contains('\\'))
		{
			var pathStr = scope:: String(path);
			pathStr.Replace('\\', '/');
			path = pathStr;
		}
		if (mFileMap.TryGetAlt(path, ?, var entry))
			return entry;
		return null;
	}

	public override bool PathPackMatches(StringView path)
	{
		return Path.Equals(path, mPath);
	}

	public override Stream OpenFile(StringView file)
	{
		return base.OpenFile(file);
	}
}
#endif

static class FilePackManager
{
	public static List<FilePack> sFilePacks = new .() ~ DeleteContainerAndItems!(_);

	public static void AddFilePack(FilePack filePack)
	{
		sFilePacks.Add(filePack);
	}

	public static void RemoveFilePack(FilePack filePack)
	{
		sFilePacks.Remove(filePack);
	}

	[StaticHook(typeof(Platform.Hook), typeof(FilePackManager.HookOverrides)), AlwaysInclude]
	public static struct Hook
	{
	}

	static StableIndexedList<Stream> sStreams = new .() ~ { DeleteContainerAndItems!(_); _ = null; }
	static StableIndexedList<FilePack.FindFileData> sFindFileData = new .() ~ { DeleteContainerAndItems!(_); _ = null; }

	static Stream TryGetStream(Platform.BfpFile* file, out int idx)
	{
		idx = (int)(void*)file;
		return sStreams?.SafeGet(idx);
	}

	static FilePack.FindFileData TryGetFindFileData(Platform.BfpFindFileData* findFileData, out int idx)
	{
		idx = (int)(void*)findFileData;
		return sFindFileData?.SafeGet(idx);
	}

	public static struct HookOverrides
	{
		[StaticHook]
		public static Platform.BfpFile* BfpFile_Create(char8* name, Platform.BfpFileCreateKind createKind, Platform.BfpFileCreateFlags createFlags, Platform.BfpFileAttributes createdFileAttrs, Platform.BfpFileResult* outResult)
		{
			if ((createKind == .OpenExisting) && (!createFlags.HasFlag(.Write)))
			{
			   if (var memory = TryGetMemory(.(name)))
				{
					var memoryStream = new FixedMemoryStream(memory);
					return (.)(void*)sStreams.Add(memoryStream);
				}
			}

			return Hook.sBfpFile_Create(name, createKind, createFlags, createdFileAttrs, outResult);
		}

		[StaticHook]
		public static int BfpFile_GetSystemHandle(Platform.BfpFile* file)
		{
			return Hook.sBfpFile_GetSystemHandle(file);
		}

		[StaticHook]
		public static void BfpFile_Release(Platform.BfpFile* file)
		{
			if (var stream = TryGetStream(file, var idx))
			{
				sStreams.RemoveAt(idx);
				delete stream;
				return;
			}

			Hook.sBfpFile_Release(file);
		}

		[StaticHook]
		public static int BfpFile_Write(Platform.BfpFile* file, void* buffer, int size, int timeoutMS, Platform.BfpFileResult* outResult)
		{
			return Hook.sBfpFile_Write(file, buffer, size, timeoutMS, outResult);
		}

		[StaticHook]
		public static int BfpFile_Read(Platform.BfpFile* file, void* buffer, int size, int timeoutMS, Platform.BfpFileResult* outResult)
		{
			if (var stream = TryGetStream(file, ?))
			{
				switch (stream.TryRead(.((.)buffer, size)))
				{
				case .Ok(let val):
					if (val != size)
					{
						if (outResult != null)
							*outResult = .PartialData;
					}
					else if (outResult != null)
						*outResult = .Ok;
					return val;
				case .Err(let err):
					if (outResult != null)
						*outResult = .UnknownError;
					return 0;
				}
			}

			return Hook.sBfpFile_Read(file, buffer, size, timeoutMS, outResult);
		}

		[StaticHook]
		public static void BfpFile_Flush(Platform.BfpFile* file)
		{
			if (var stream = TryGetStream(file, ?))
			{
				return;
			}

			Hook.sBfpFile_Flush(file);
		}

		[StaticHook]
		public static int64 BfpFile_GetFileSize(Platform.BfpFile* file)
		{
			if (var stream = TryGetStream(file, ?))
			{
				return stream.Length;
			}

			return Hook.sBfpFile_GetFileSize(file);
		}

		[StaticHook]
		public static int64 BfpFile_Seek(Platform.BfpFile* file, int64 offset, Platform.BfpFileSeekKind seekKind)
		{
			if (var stream = TryGetStream(file, ?))
			{
				stream.Seek(offset, (.)seekKind);
				return stream.Position;
			}

			return Hook.sBfpFile_Seek(file, offset, seekKind);

		}

		[StaticHook]
		public static void BfpFile_Truncate(Platform.BfpFile* file, Platform.BfpFileResult* outResult)
		{
			Hook.sBfpFile_Truncate(file, outResult);
		}

		[StaticHook]
		public static Platform.BfpFindFileData* BfpFindFileData_FindFirstFile(char8* path, Platform.BfpFindFileFlags flags, Platform.BfpFileResult* outResult)
		{
			StringView pathStr = .(path);

			String findDir = scope .();
			Path.GetDirectoryPath(pathStr, findDir).IgnoreError();
			findDir.Append("/");
			String wildcard = scope .();
			Path.GetFileName(pathStr, wildcard);

			if (var packFile = FindFilePackEntry(findDir))
			{
				var packFindFile = packFile.CreateFindFileData(wildcard, (.)flags);
				if (!packFindFile.MoveNext())
				{
					delete packFile;
					if (outResult != null)
						*outResult = .NoResults;
					return null;
				}
				return (.)(void*)sFindFileData.Add(packFindFile);
			}

			return Hook.sBfpFindFileData_FindFirstFile(path, flags, outResult);
		}

		[StaticHook]
		public static bool BfpFindFileData_FindNextFile(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.MoveNext();
			}

			return Hook.sBfpFindFileData_FindNextFile(findData);
		}

		[StaticHook]
		public static void BfpFindFileData_GetFileName(Platform.BfpFindFileData* findData, char8* outName, int32* inOutNameSize, Platform.BfpFileResult* outResult)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				var str = packFindFile.GetFileName(.. scope .());
				Platform.SetStrHelper(str, outName, inOutNameSize, (.)outResult);
				return;
			}

			Hook.sBfpFindFileData_GetFileName(findData, outName, inOutNameSize, outResult);
		}

		[StaticHook]
		public static Platform.BfpTimeStamp BfpFindFileData_GetTime_LastWrite(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.GetWriteTime();
			}

			return Hook.sBfpFindFileData_GetTime_LastWrite(findData);
		}

		[StaticHook]
		public static Platform.BfpTimeStamp BfpFindFileData_GetTime_Created(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.GetCreatedTime();
			}

			return Hook.sBfpFindFileData_GetTime_Created(findData);
		}

		[StaticHook]
		public static Platform.BfpTimeStamp BfpFindFileData_GetTime_Access(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.GetAccessTime();
			}

			return Hook.sBfpFindFileData_GetTime_Access(findData);
		}

		[StaticHook]
		public static Platform.BfpFileAttributes BfpFindFileData_GetFileAttributes(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.GetFileAttributes();
			}

			return Hook.sBfpFindFileData_GetFileAttributes(findData);
		}

		[StaticHook]
		public static int64 BfpFindFileData_GetFileSize(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, ?))
			{
				return packFindFile.GetFileSize();
			}

			return Hook.sBfpFindFileData_GetFileSize(findData);
		}

		[StaticHook]
		public static void BfpFindFileData_Release(Platform.BfpFindFileData* findData)
		{
			if (var packFindFile = TryGetFindFileData(findData, var idx))
			{
				sFindFileData.RemoveAt(idx);
				delete packFindFile;
				return;
			}

			Hook.sBfpFindFileData_Release(findData);
		}
	}

	public static FilePack.FileEntry FindFilePackEntry(StringView path)
	{
		if (path.StartsWith('['))
		{
			int packEndIdx = path.IndexOf(']');

			if (packEndIdx != -1)
			{
				var packPath = path.Substring(1, packEndIdx - 1);
				var filePath = path.Substring(packEndIdx + 1);
				for (var filePack in sFilePacks)
				{
					if (filePack.PathPackMatches(packPath))
					{
						return filePack.GetFileEntry(filePath);
					}
				}
			}
		}
		return null;
	}

	public static Result<Span<uint8>> TryGetMemory(StringView path)
	{
		var fileEntry = FindFilePackEntry(path);
		if (fileEntry == null)
			return .Err;

		return fileEntry.GetFileData();
	}

	public static bool TryMakeMemoryString(String path)
	{
		if (TryGetMemory(path) case .Ok(let val))
		{
			String prevPath = scope .()..Append(path);

			var ext = Path.GetExtension(path, .. scope .());
			path.Set(scope $"@{(int)(void*)val.Ptr:X}:{val.Length}:{prevPath}");
			return true;
		}
		return false;
	}

	public static bool TryMakeMemoryString(String path, params Span<StringView> checkExts)
	{
		var ext = Path.GetExtension(path, .. scope .());
		if (!ext.IsEmpty)
			return TryMakeMemoryString(path);

		for (var checkExt in checkExts)
		{
			var testPath = scope String();
			testPath.Append(path);
			testPath.Append(checkExt);
			if (TryMakeMemoryString(testPath))
			{
				path.Set(testPath);
				return true;
			}	
		}
		return false;
	}
}