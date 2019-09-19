using System.Threading;
using System.Diagnostics.Contracts;
using System.Diagnostics;

namespace System.IO
{
	abstract class FileStreamBase : Stream
	{
		protected Platform.BfpFile* mBfpFile;

		public override int64 Position
		{
			get
			{
				return Platform.BfpFile_Seek(mBfpFile, 0, .Relative);
			}

			set
			{
				Platform.BfpFile_Seek(mBfpFile, value, .Absolute);
			}
		}

		public override int64 Length
		{
			get
			{
				return Platform.BfpFile_GetFileSize(mBfpFile);
			}
		}

		public ~this()
		{
			Close();
		}

		public override Result<void> Seek(int64 pos, SeekKind seekKind = .Absolute)
		{
			int64 newPos = Platform.BfpFile_Seek(mBfpFile, pos, (Platform.BfpFileSeekKind)seekKind);
			// Ensure position is what was requested
			if ((seekKind == .Absolute) && (newPos != pos))
				return .Err;
			return .Ok;
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Read(mBfpFile, data.Ptr, data.Length, -1, &result);
			if ((result != .Ok) && (result != .PartialData))
				return .Err;
			return numBytesRead;
		}

		public Result<int> TryRead(Span<uint8> data, int timeoutMS)
		{
			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Read(mBfpFile, data.Ptr, data.Length, timeoutMS, &result);
			if ((result != .Ok) && (result != .PartialData))
				return .Err;
			return numBytesRead;
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			Platform.BfpFileResult result = .Ok;
			int numBytesWritten = Platform.BfpFile_Write(mBfpFile, data.Ptr, data.Length, -1, &result);
			if ((result != .Ok) && (result != .PartialData))
				return .Err;
			return numBytesWritten;
		}

		public override void Close()
		{
			if (mBfpFile != null)
				Platform.BfpFile_Release(mBfpFile);
			mBfpFile = null;
		}

		public override void Flush()
		{
			if (mBfpFile != null)
				Platform.BfpFile_Flush(mBfpFile);
		}
	}

	class FileStream : FileStreamBase
	{
		FileAccess mFileAccess;

		public this()
		{
		}

		public this(Platform.BfpFile* handle, FileAccess access, int32 bufferSize, bool isAsync)
		{
			mBfpFile = handle;
			mFileAccess = access;

		}

		public override bool CanRead
		{
			get
			{
				return mFileAccess.HasFlag(FileAccess.Read);
			}
		}

		public override bool CanWrite
		{
			get
			{
				return mFileAccess.HasFlag(FileAccess.Write);
			}
		}

		public Result<void, FileOpenError> Create(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options= .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Create, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> Open(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options= .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Open, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> OpenStd(Platform.BfpFileStdKind stdKind)
		{
			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_GetStd(stdKind, &fileResult);

			if ((mBfpFile == null) || (fileResult != .Ok))
			{
				switch (fileResult)
				{
				case .ShareError:
					return .Err(.SharingViolation);
				case .NotFound:
					return .Err(.NotFound);
				default:
					return .Err(.Unknown);
				}
			}
			return .Ok;
		}

		public Result<void, FileOpenError> Open(StringView path, FileMode mode, FileAccess access, FileShare share = .None, int bufferSize = 4096, FileOptions options= .None, SecurityAttributes* secAttrs = null)
		{
			Runtime.Assert(mBfpFile == null);

			Platform.BfpFileCreateKind createKind = .CreateAlways;
			Platform.BfpFileCreateFlags createFlags = .None;

			switch (mode)
			{
			case .CreateNew:
				createKind = .CreateIfNotExists;
			case .Create:
				createKind = .CreateAlways;
			case .Open:
				createKind = .OpenExisting;
			case .OpenOrCreate:
				createKind = .CreateAlways;
			case .Truncate:
				createKind = .CreateAlways;
				createFlags |= .Truncate;
			case .Append:
				createKind = .CreateAlways;
				createFlags |= .Append;
			}

			if (access.HasFlag(.Read))
				createFlags |= .Read;
			if (access.HasFlag(.Write))
				createFlags |= .Write;

			if (share.HasFlag(.Read))
				createFlags |= .ShareRead;
			if (share.HasFlag(.Write))
				createFlags |= .ShareWrite;
			if (share.HasFlag(.Delete))
				createFlags |= .ShareDelete;

			Platform.BfpFileAttributes fileFlags = .Normal;
			
			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_Create(path.ToScopeCStr!(128), createKind, createFlags, fileFlags, &fileResult);

			if ((mBfpFile == null) || (fileResult != .Ok))
			{
				switch (fileResult)
				{
				case .ShareError:
					return .Err(.SharingViolation);
				case .NotFound:
					return .Err(.NotFound);
				default:
					return .Err(.Unknown);
				}
			}

			return .Ok;
		}

		public void Attach(Platform.BfpFile* bfpFile)
		{
			Close();
			mBfpFile = bfpFile;
		}
	}
}
