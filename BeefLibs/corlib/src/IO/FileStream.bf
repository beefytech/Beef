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

		public virtual Result<int> TryRead(Span<uint8> data, int timeoutMS)
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

		// This is not guaranteed to actually represent the file
		uint8[] mBuffer;
		int64 mPosInBuffer;
		// How far the file contents (not things in buffer) reach into the buffer. Only used in reading, -1 means we didn't need to read yet
		int64 mBufferReadCount = -1;
		// How much of the buffer has been changed. We don't actually have written here yet. Only used in writing
		int64 mBufferWriteCount;

		// We need to delete mBuffer in our parent's call to Close()
		// (because otherwise it's already deleted when we still have to access it there and that function needs to know we're calling from a destructor)
		bool mDeleting;

		public this()
		{
		}

		public ~this()
		{
			mDeleting = true;
		}

		public this(Platform.BfpFile* handle, FileAccess access, int32 bufferSize, bool isAsync)
		{
			mBfpFile = handle;
			mFileAccess = access;
			MakeBuffer(bufferSize);
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

			MakeBuffer(0);

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
			mFileAccess = access;

			MakeBuffer(bufferSize);

			return .Ok;
		}

		public void Attach(Platform.BfpFile* bfpFile, FileAccess access = .ReadWrite, int32 bufferSize = 4096)
		{
			Close();
			mBfpFile = bfpFile;
			mFileAccess = access;

			MakeBuffer(bufferSize);
		}

		public override void Close()
		{
			if (mBufferWriteCount != 0)
				CommitBuffer();

			base.Close();
			mFileAccess = default;

			// Reset for potential next use
			mPosInBuffer = 0;
			mBufferReadCount = -1;
			mBufferWriteCount = 0;

			if (mDeleting && mBuffer != null)
				delete mBuffer;
		}

		[Inline]
		void MakeBuffer(int bufferSize)
		{
			Debug.Assert(bufferSize >= 0);

			mBuffer = new .[bufferSize];
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			if (!mFileAccess.HasFlag(FileAccess.Write))
				return .Err;

			Debug.Assert(data.Ptr != null);
			Debug.Assert(data.Length > 0);

			// Write into buffer
			if (data.Length < mBuffer.Count)
			{
				// Use full buffer capacity if necessary
				if (data.Length > mBuffer.Count - mPosInBuffer)
				{
					if (mBufferWriteCount != 0)
						Try!(CommitBuffer());
					else
						Try!(SkipBuffer());
				}	

				// Copy into buffer
				Internal.MemCpy(&mBuffer[mPosInBuffer], data.Ptr, data.Length);

				mPosInBuffer += data.Length;

				if (mPosInBuffer > mBufferWriteCount)
					mBufferWriteCount = mPosInBuffer;

				return data.Length;
			}
			else // Write directly
			{
				// Manage buffer
				if (mBufferWriteCount != 0)
					Try!(CommitBuffer());
				else if (mPosInBuffer != 0)
					Try!(SkipBuffer());

				return base.TryWrite(data);
			}
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			if (!mFileAccess.HasFlag(FileAccess.Read))
				return .Err;

			Debug.Assert(data.Ptr != null);
			Debug.Assert(data.Length > 0);

			// Read from buffer
			if (data.Length < mBuffer.Count)
			{
				return TryReadBuffer(data);
			}
			else // Read directly
			{
				// Manage buffer
				if (mBufferWriteCount != 0)
					Try!(CommitBuffer());
				else if (mPosInBuffer != 0)
					Try!(SkipBuffer());

				return base.TryRead(data);
			}
		}

		public override Result<int> TryRead(Span<uint8> data, int timeoutMS)
		{
			if (!mFileAccess.HasFlag(FileAccess.Read))
				return .Err;

			Debug.Assert(data.Ptr != null);
			Debug.Assert(data.Length > 0);

			// Read from buffer
			if (data.Length < mBuffer.Count)
			{
				return TryReadBuffer(data);
			}
			else // Read directly
			{
				// Manage buffer
				if (mBufferWriteCount != 0)
					Try!(CommitBuffer());
				else if (mPosInBuffer != 0)
					Try!(SkipBuffer());

				return base.TryRead(data, timeoutMS);
			}
		}

		Result<int> TryReadBuffer(Span<uint8> data)
		{
			// Use full buffer capacity if necessary
			if (data.Length > mBuffer.Count - mPosInBuffer)
			{
				if (mBufferWriteCount != 0)
					Try!(CommitBuffer());
				else
					Try!(SkipBuffer());
			}

			if (mBufferReadCount == -1 && mPosInBuffer + data.Length >= mBufferWriteCount)
				Try!(FillBuffer()); // We haven't actually read into the buffer yet. Sets mBufferFill

			var readLength = data.Length;
			let maxRead = Math.Max(mBufferReadCount, mBufferWriteCount);
			if (mPosInBuffer + readLength > maxRead) // Make sure we only read as far as we can
			{
				readLength += mPosInBuffer - maxRead; // (mPosInBuffer - maxRead) is a negative value

				if (readLength <= 0) // We can't read anything
					return 0;
			}

			Internal.MemCpy(data.Ptr, &mBuffer[mPosInBuffer], readLength);
			mPosInBuffer += readLength;

			return readLength;
		}

		Result<void> SkipBuffer()
		{
			Debug.Assert(mBufferWriteCount == 0);

			// Seek past current buffer
			let res = base.Seek(mPosInBuffer, .Relative);

			// Reset buffer
			mPosInBuffer = 0;
			mBufferReadCount = -1;
			mBufferWriteCount = 0;

			Try!(res);

			return .Ok;
		}

		Result<void> CommitBuffer()
		{
			// Store for comparison
			let writeLen = mBufferWriteCount;

			let res = base.TryWrite(.(&mBuffer[0], writeLen));

			// Go back to mPosInBuffer for next buffer position
			if (mPosInBuffer < mBufferWriteCount)
				base.Seek(mPosInBuffer - mBufferWriteCount, .Relative);

			mPosInBuffer = 0;
			mBufferReadCount = -1;
			mBufferWriteCount = 0;

			if (res case .Ok(let val))
			{
				if (val != writeLen)
					return .Err;

				return .Ok;
			}
			else return .Err;
		}

		Result<void> FillBuffer()
		{
			// Skip modified bits, those are overwritten anyway
			base.Seek(mBufferWriteCount, .Relative);

			// Fill up unmodified buffer for reading
			let res = base.TryRead(Span<uint8>(&mBuffer[mBufferWriteCount], mBuffer.Count - mBufferWriteCount));

			if (res case .Ok(let val))
			{
				// Move back to where we were before
				base.Seek(-val - mBufferWriteCount, .Relative);

				mBufferReadCount = val;
				return .Ok;
			}
			else return .Err;
		}

		public override int64 Position
		{
			get
			{
				return Platform.BfpFile_Seek(mBfpFile, 0, .Relative) + mPosInBuffer;
			}

			set
			{
				SeekNotRelative(value, Platform.BfpFile_Seek(mBfpFile, 0, .Relative));
			}
		}

		int64 SeekNotRelative(int64 value, int64 bufPosInFile)
		{
			if (value >= bufPosInFile && value < bufPosInFile + mBuffer.Count)
			{
				// If we skip forward a bit we need to call FillBuffer if we haven't read into the buffer yet,
				// otherwise we may 0 out previously existing stuff in the file (since we only the max write index
				// and assume that we write without gaps, we can't produce some here)
				if (mFileAccess.HasFlag(.Write) && mBufferReadCount == -1 && value - bufPosInFile > mPosInBuffer)
				{
					if (mFileAccess.HasFlag(.Read))
						FillBuffer();
					else
					{
						// We cant read, so we really need to move the buffer instead
						return SeekNotRelativeOutsideBuffer(value);
					}
				}
				
				// "Seek" inside the buffer
				mPosInBuffer = value - bufPosInFile;
				return value;
			}
			else
			{
				return SeekNotRelativeOutsideBuffer(value);
			}
		}

		int64 SeekNotRelativeOutsideBuffer(int64 value)
		{
			if (mBufferWriteCount != 0)
				CommitBuffer();
			else
			{
				// Reset
				mPosInBuffer = 0;
				mBufferReadCount = -1;
			}

			// Next buffer will be acting from here
			return Platform.BfpFile_Seek(mBfpFile, value, .Absolute);
		}

		public override int64 Length
		{
			get
			{
				let fileLength = Platform.BfpFile_GetFileSize(mBfpFile);
				
				if (mBufferWriteCount != 0)
				{
					let bufPosInFile = Platform.BfpFile_Seek(mBfpFile, 0, .Relative);
					if (bufPosInFile + mBufferWriteCount > fileLength)
						return bufPosInFile + mBufferWriteCount;
				}

				return fileLength;
			}
		}

		public override Result<void> Seek(int64 pos, SeekKind seekKind = .Absolute)
		{
			let bufPosInFile = Platform.BfpFile_Seek(mBfpFile, 0, .Relative);

			var actualPos = pos;
			if (seekKind == .Relative)
				actualPos += bufPosInFile + mPosInBuffer;
			else if (seekKind == .FromEnd)
				actualPos += Length;

			let newPos = SeekNotRelative(actualPos, bufPosInFile);

			if (actualPos != newPos)
				return .Err;
			return .Ok;
		}

		public override void Flush()
		{
			if (mBufferWriteCount != 0)
				CommitBuffer();

			if (mBfpFile != null)
				Platform.BfpFile_Flush(mBfpFile);
		}
	}
}
