using System.Collections;
using System.Text;
using System.Diagnostics;

namespace System.IO
{
	public enum FileOpenError
	{
		Unknown,
		NotFound,
		NotFile,
		SharingViolation
	}

	public enum FileReadError
	{
		Unknown,
		Timeout
	}

	public enum FileError
	{
		case Unknown;
		case OpenError(FileOpenError);
		case ReadError(FileReadError);
		case SeekError;
	}

	static class File
	{
		public static Result<void, FileError> ReadAll(StringView path, List<uint8> outData)
		{
			UnbufferedFileStream fs = scope UnbufferedFileStream();
			var result = fs.Open(path, .Open, .Read, .ReadWrite);
			if (result case .Err(let err))
				return .Err(.OpenError(err));

			int64 fileSize = fs.Length;
			outData.Reserve((.)fileSize);

			while (true)
			{
				uint8[8192] buffer;
				switch (fs.TryRead(.(&buffer, 8192)))
				{
				case .Ok(let bytes):
					if (bytes == 0)
						return .Ok;
					outData.AddRange(.(&buffer, bytes));
				case .Err:
					return .Err(.Unknown);
				}
			}
		}

		public static Result<void> WriteAll(StringView path, Span<uint8> data, bool doAppend = false)
		{
			UnbufferedFileStream fs = scope UnbufferedFileStream();
			var result = fs.Open(path, doAppend ? .Append : .Create, .Write);
			if (result case .Err)
				return .Err;
			fs.TryWrite(.((uint8*)data.Ptr, data.Length));
			return .Ok;
		}

		public static Result<void, FileError> ReadAllText(StringView path, String outText, bool preserveLineEnding = false)
		{
			StreamReader sr = scope StreamReader();
			if (sr.Open(path) case .Err(let err))
				return .Err(.OpenError(err));
            if (sr.ReadToEnd(outText) case .Err)
				return .Err(.ReadError(.Unknown));

			if (!preserveLineEnding)
			{
				if (Environment.NewLine.Length > 1)
					outText.Replace("\r", "");
			}

			return .Ok;
		}

		public static Result<void> WriteAllText(StringView path, StringView text, bool doAppend = false)
		{
			UnbufferedFileStream fs = scope UnbufferedFileStream();
			var result = fs.Open(path, doAppend ? .Append : .Create, .Write);
			if (result case .Err)
				return .Err;
			if (fs.TryWrite(.((uint8*)text.Ptr, text.Length)) case .Err(let err))
				return .Err(err);
			return .Ok;
		}

		public static Result<void> WriteAllText(StringView path, StringView text, Encoding encoding)
		{
			UnbufferedFileStream fs = scope UnbufferedFileStream();

			int len = encoding.GetEncodedSize(text);
			uint8* data = new uint8[len]*;
			defer delete data;
			int actualLen = encoding.Encode(text, .(data, len));
			Debug.Assert(len == actualLen);
			if (len != actualLen)
				return .Err;

			var result = fs.Open(path, .Create, .Write);
			if (result case .Err)
				return .Err;
			fs.TryWrite(.(data, len));
			return .Ok;
		}

		public static Result<void> WriteAllLines(StringView path, IEnumerator<StringView> enumerator, bool doAppend = false)
		{
			String strBuf = scope String();
			for (var str in enumerator)
			{
				strBuf.Append(str);
				strBuf.Append(Environment.NewLine);
			}
			return WriteAllText(path, strBuf, doAppend);
		}

		public static Result<void> WriteAllLines(StringView path, IEnumerator<String> enumerator, bool doAppend = false)
		{
			String strBuf = scope String();
			for (var str in enumerator)
			{
				strBuf.Append(str);
				strBuf.Append(Environment.NewLine);
			}
			return WriteAllText(path, strBuf, doAppend);
		}

		/*public static Result<IEnumerator<Result<String>>> ReadLines(String fileName)
		{
			ThrowUnimplemented();
			return (IEnumerator<Result<String>>)null;
		}*/

		public static bool Exists(StringView fileName)
		{
			return Platform.BfpFile_Exists(fileName.ToScopeCStr!());
		}

		public static Result<void, Platform.BfpFileResult> Delete(StringView fileName)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_Delete(fileName.ToScopeCStr!(), &result);
			if ((result != .Ok) && (result != .NotFound))
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> Move(StringView fromPath, StringView toPath)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_Rename(fromPath.ToScopeCStr!(), toPath.ToScopeCStr!(), &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> Copy(StringView fromPath, StringView toPath)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_Copy(fromPath.ToScopeCStr!(), toPath.ToScopeCStr!(), .Always, &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> CopyIfNewer(StringView fromPath, StringView toPath)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_Copy(fromPath.ToScopeCStr!(), toPath.ToScopeCStr!(), .IfNewer, &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> Copy(StringView fromPath, StringView toPath, bool overwrite)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_Copy(fromPath.ToScopeCStr!(), toPath.ToScopeCStr!(), overwrite ? .Always : .IfNotExists, &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		public static Result<void, Platform.BfpFileResult> SetAttributes(StringView path, FileAttributes attr)
		{
			Platform.BfpFileResult result = default;
			Platform.BfpFile_SetAttributes(path.ToScopeCStr!(), (Platform.BfpFileAttributes)attr, &result);
			if (result != .Ok)
				return .Err(result);
			return .Ok;
		}

		public static Result<DateTime> GetLastWriteTime(StringView fullPath)
		{
			return DateTime.FromFileTime((int64)Platform.BfpFile_GetTime_LastWrite(fullPath.ToScopeCStr!()));
		}

		public static Result<DateTime> GetLastWriteTimeUtc(StringView fullPath)
		{
			return DateTime.FromFileTimeUtc((int64)Platform.BfpFile_GetTime_LastWrite(fullPath.ToScopeCStr!()));
		}
	}
}
