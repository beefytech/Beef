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

		public int Handle
		{
			get
			{
				if (mBfpFile == null)
					return 0;
				return Platform.BfpFile_GetSystemHandle(mBfpFile);
			}
		}

		public ~this()
		{
			Delete();
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

		public override Result<int, FileError> TryRead(Span<uint8> data, int timeoutMS)
		{
			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Read(mBfpFile, data.Ptr, data.Length, timeoutMS, &result);
			if ((result != .Ok) && (result != .PartialData))
			{
				switch (result)
				{
				case .Timeout:
					return .Err(.ReadError(.Timeout));
				default:
					return .Err(.ReadError(.Unknown));
				}
			}
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

		public override Result<void> Close()
		{
			if (mBfpFile != null)
				Platform.BfpFile_Release(mBfpFile);
			mBfpFile = null;
			return .Ok;
		}

		public override Result<void> Flush()
		{
			if (mBfpFile != null)
				Platform.BfpFile_Flush(mBfpFile);
			return .Ok;
		}

		protected virtual void Delete()
		{
			Close();
		}
	}

	interface IFileStream
	{
		Result<void> Attach(Platform.BfpFile* bfpFile, FileAccess access = .ReadWrite);
	}

	class UnbufferedFileStream : FileStreamBase, IFileStream
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

		public Result<void, FileOpenError> Create(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Create, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> Open(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Open, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> OpenStd(Platform.BfpFileStdKind stdKind)
		{
			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_GetStd(stdKind, &fileResult);
			mFileAccess = .ReadWrite;

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

		public Result<void, FileOpenError> Open(StringView path, FileMode mode, FileAccess access, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
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
				createFlags |= .Truncate;
			case .Open:
				createKind = .OpenExisting;
			case .OpenOrCreate:
				createKind = .OpenAlways;
			case .Truncate:
				createKind = .OpenExisting;
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
			mFileAccess = access;

			return .Ok;
		}

		public Result<void> Attach(Platform.BfpFile* bfpFile, FileAccess access = .ReadWrite)
		{
			Close();
			mBfpFile = bfpFile;
			mFileAccess = access;
			return .Ok;
		}

		public override Result<void> Close()
		{
			mFileAccess = default;
			if (base.Close() case .Err)
				return .Err;
			return .Ok;
		}

		public override Result<void> SetLength(int64 length)
		{
			int64 pos = Position;

			if (pos != length)
				Seek(length);

			Platform.BfpFileResult result = .Ok;
			Platform.BfpFile_Truncate(mBfpFile, &result);
			if (result != .Ok)
			{
				Seek(pos);
				return .Err;
			}

			if (pos != length)
			{
				if (pos < length)
					Seek(pos);
				else
					Seek(0, .FromEnd);
			}

			return .Ok;
		}
	}

	class BufferedFileStream : BufferedStream, IFileStream
	{
		protected Platform.BfpFile* mBfpFile;
		protected int64 mBfpFilePos;
		FileAccess mFileAccess;

		public int Handle
		{
			get
			{
				if (mBfpFile == null)
					return 0;
				return Platform.BfpFile_GetSystemHandle(mBfpFile);
			}
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

		public override int64 Position
		{
			set
			{
				// Matches the behavior of Platform.BfpFile_Seek(mBfpFile, value, .Absolute);
				mPos = Math.Max(value, 0);
			}
		}

		public this()
		{
			
		}

		public ~this()
		{
			Delete();
		}

		protected virtual void Delete()
		{
			Close();
		}

		public this(Platform.BfpFile* handle, FileAccess access, int32 bufferSize, bool isAsync)
		{
			mBfpFile = handle;
			mFileAccess = access;
		}

		public Result<void, FileOpenError> Create(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Create, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> Open(StringView path, FileAccess access = .ReadWrite, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
		{
			return Open(path, FileMode.Open, access, share, bufferSize, options, secAttrs);
		}

		public Result<void, FileOpenError> OpenStd(Platform.BfpFileStdKind stdKind)
		{
			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_GetStd(stdKind, &fileResult);
			mFileAccess = .ReadWrite;

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

		public Result<void, FileOpenError> Open(StringView path, FileMode mode, FileAccess access, FileShare share = .None, int bufferSize = 4096, FileOptions options = .None, SecurityAttributes* secAttrs = null)
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
				createKind = .OpenAlways;
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
			mFileAccess = access;

			MakeBuffer(bufferSize);

			return .Ok;
		}

		public Result<void> Attach(Platform.BfpFile* bfpFile, FileAccess access = .ReadWrite)
		{
			Close();
			mBfpFile = bfpFile;
			mFileAccess = access;
			return .Ok;
		}

		public override Result<void> Seek(int64 pos, SeekKind seekKind = .Absolute)
		{
			int64 newPos;
			switch (seekKind)
			{
			case .Absolute:
				newPos = pos;
			case .FromEnd:
				newPos = Length + pos;
			case .Relative:
				newPos = mPos + pos;
			}

			// Matches the behaviour of Platform.BfpFile_Seek(mBfpFile, value, .Absolute);
			mPos = Math.Max(newPos, 0);
			if (seekKind == .Absolute && newPos < 0)
				return .Err;

			return .Ok;
		}

		public override Result<void> Close()
		{
			let ret = base.Close();
			if (mBfpFile != null)
				Platform.BfpFile_Release(mBfpFile);

			mBfpFile = null;
			mFileAccess = default;
			mBfpFilePos = 0;
			return ret;
		}

		protected override void UpdateLength()
		{
			mUnderlyingLength = Platform.BfpFile_GetFileSize(mBfpFile);
		}

		protected Result<void, FileError> SeekUnderlying(int64 offset, Platform.BfpFileSeekKind seekKind = .Absolute)
		{
			int64 newPos = Platform.BfpFile_Seek(mBfpFile, offset, seekKind);
			Result<void, FileError> result = ((seekKind == .Absolute) && (newPos != offset)) ? .Err(.SeekError) : .Ok;
			if (result case .Ok)
				mBfpFilePos = newPos;
			return result;
		}

		protected override Result<int> TryReadUnderlying(int64 pos, Span<uint8> data)
		{
			if (mBfpFilePos != pos)
				Try!(SeekUnderlying(pos));

			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Read(mBfpFile, data.Ptr, data.Length, -1, &result);
			if ((result != .Ok) && (result != .PartialData))
				return .Err;
			mBfpFilePos += numBytesRead;
			return numBytesRead;
		}

		protected override Result<int> TryWriteUnderlying(int64 pos, Span<uint8> data)
		{
			if (mBfpFilePos != pos)
				Try!(SeekUnderlying(pos));

			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Write(mBfpFile, data.Ptr, data.Length, -1, &result);
			if ((result != .Ok) && (result != .PartialData))
				return .Err;
			mBfpFilePos += numBytesRead;
			return numBytesRead;
		}

		public override Result<int, FileError> TryRead(Span<uint8> data, int timeoutMS)
		{
			if (mBfpFilePos != mPos)
				Try!(SeekUnderlying(mPos));

			Platform.BfpFileResult result = .Ok;
			int numBytesRead = Platform.BfpFile_Read(mBfpFile, data.Ptr, data.Length, timeoutMS, &result);
			if ((result != .Ok) && (result != .PartialData))
			{
				switch (result)
				{
				case .Timeout:
					return .Err(.ReadError(.Timeout));
				default:
					return .Err(.ReadError(.Unknown));
				}
			}
			return numBytesRead;
		}

		public override Result<void> SetLength(int64 length)
		{
			Try!(Flush());

			int64 pos = Position;

			if (pos != length || pos != mBfpFilePos)
			{
				Try!(SeekUnderlying(length));
				mPos = length;
			}

			Platform.BfpFileResult result = .Ok;
			Platform.BfpFile_Truncate(mBfpFile, &result);
			if (result != .Ok)
			{
				Try!(SeekUnderlying(pos));
				return .Err;
			}

			mUnderlyingLength = length;
			mPos = Math.Min(pos, Length);

			if (pos != length)
			{
				if (pos < length)
					Try!(SeekUnderlying(pos));
				else
					Try!(SeekUnderlying(0, .FromEnd));
			}

			return .Ok;
		}

		public override Result<void> Flush()
		{
			var result = base.Flush();
			if (mBfpFile != null)
				Platform.BfpFile_Flush(mBfpFile);
			return result;
		}
	}

	class FileStream : BufferedFileStream
	{

	}
}
