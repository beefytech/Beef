using System.Collections;
using System.Text;
using System.Diagnostics;

namespace System.IO
{
	public enum FileOpenError
	{
		NotFound,
		NotFile,
		Unknown,
		SharingViolation
	}

	public enum FileReadError
	{
		Unknown
	}

	public enum FileError
	{
		case FileOpenError(FileOpenError);
		case FileReadError(FileReadError);
	}

	class File
	{
		public static Result<void, FileError> ReadAllText(StringView path, String outText, bool preserveLineEnding = false)
		{
			StreamReader sr = scope StreamReader();
			if (sr.Open(path) case .Err(let err))
				return .Err(.FileOpenError(err));
            if (sr.ReadToEnd(outText) case .Err)
				return .Err(.FileReadError(.Unknown));

			if (!preserveLineEnding)
			{
				if (Environment.NewLine.Length > 1)
					outText.Replace("\r", "");
			}

			return .Ok;
		}

		public static Result<void> WriteAllText(StringView path, StringView text, bool doAppend = false)
		{
			FileStream fs = scope FileStream();
			var result = fs.Open(path, doAppend ? .Append : .Create, .Write);
			if (result case .Err)
				return .Err;
			fs.TryWrite(.((uint8*)text.Ptr, text.Length));
			return .Ok;
		}

		public static Result<void> WriteAllText(StringView path, StringView text, Encoding encoding)
		{
			FileStream fs = scope FileStream();

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

		public static Result<void> WriteAllLines(StringView path, IEnumerator<StringView> enumerator)
		{
			String strBuf = scope String();
			for (var str in enumerator)
			{
				strBuf.Append(str);
				strBuf.Append(Environment.NewLine);
			}
			return WriteAllText(path, strBuf);
		}

		public static Result<void> WriteAllLines(StringView path, IEnumerator<String> enumerator)
		{
			String strBuf = scope String();
			for (var str in enumerator)
			{
				strBuf.Append(str);
				strBuf.Append(Environment.NewLine);
			}
			return WriteAllText(path, strBuf);
		}

		/*public static Result<IEnumerator<Result<String>>> ReadLines(String fileName)
		{
			ThrowUnimplemented();
			return (IEnumerator<Result<String>>)null;
		}*/

		static extern bool Exists(char8* fileName);

		public static bool Exists(StringView fileName)
		{
			return Exists(fileName.ToScopeCStr!());
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
