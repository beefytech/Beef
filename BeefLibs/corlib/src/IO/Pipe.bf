namespace System.IO
{
	enum PipeOptions
	{
		None = 0,
		AllowTimeouts = 1
	}

	class NamedPipe : FileStreamBase
	{
		public override bool CanRead
		{
			get
			{
				return true;
			}
		}

		public override bool CanWrite
		{
			get
			{
				return true;
			}
		}

		public Result<void, FileOpenError> Create(StringView machineName, StringView pipeName, PipeOptions options)
		{
			Close();

			Runtime.Assert(mBfpFile == null);

			String path = scope String();
			path.Append(pipeName);

			Platform.BfpFileCreateKind createKind = .CreateAlways;
			Platform.BfpFileCreateFlags createFlags = .Pipe;

			if (options.HasFlag(.AllowTimeouts))
				createFlags |= .AllowTimeouts;

			createKind = .CreateIfNotExists;
			createFlags |= .Read;
			createFlags |= .Write;

			Platform.BfpFileAttributes fileFlags = .Normal;

			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_Create(path, createKind, createFlags, fileFlags, &fileResult);

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

		public Result<void, FileOpenError> Open(StringView machineName, StringView pipeName, PipeOptions options)
		{
			Close();

			Runtime.Assert(mBfpFile == null);

			String path = scope String();
			path.Append(pipeName);

			Platform.BfpFileCreateKind createKind = .CreateAlways;
			Platform.BfpFileCreateFlags createFlags = .Pipe;

			createKind = .OpenExisting;
			createFlags |= .Read;
			createFlags |= .Write;

			Platform.BfpFileAttributes fileFlags = .Normal;

			Platform.BfpFileResult fileResult = .Ok;
			mBfpFile = Platform.BfpFile_Create(path, createKind, createFlags, fileFlags, &fileResult);

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
	}
}
