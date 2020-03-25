using System.Text;
using System.IO;
using System.Threading;

namespace System
{
	public static class Console
	{
		public static Encoding InputEncoding = Encoding.ASCII;
		public static Encoding OutputEncoding = Encoding.ASCII;
		
		static StreamWriter OpenStreamWriter(Platform.BfpFileStdKind stdKind, ref StreamWriter outStreamWriter)
		{
			if (outStreamWriter == null)
			{
				FileStream fileStream = new .();
				Stream stream = fileStream;
				if (fileStream.OpenStd(stdKind) case .Err)
				{
					DeleteAndNullify!(fileStream);
					stream = new NullStream();
				}

				StreamWriter newStreamWriter = new StreamWriter(stream, InputEncoding, 4096, true);
				newStreamWriter.AutoFlush = true;

				let prevValue = Interlocked.CompareExchange(ref outStreamWriter, null, newStreamWriter);
				if (prevValue != null)
				{
					// This was already set - race condition
					delete newStreamWriter;
					return prevValue;
				}
				return newStreamWriter;
			}
			return outStreamWriter;
		}

		static StreamReader OpenStreamReader(Platform.BfpFileStdKind stdKind, ref StreamReader outStreamReader)
		{
			if (outStreamReader == null)
			{
				FileStream fileStream = new .();
				Stream stream = fileStream;
				if (fileStream.OpenStd(stdKind) case .Err)
				{
					DeleteAndNullify!(fileStream);
					stream = new NullStream();
				}

				StreamReader newStreamReader = new StreamReader(stream, InputEncoding, false, 4096, true);

				let prevValue = Interlocked.CompareExchange(ref outStreamReader, null, newStreamReader);
				if (prevValue != null)
				{
					// This was already set - race condition
					delete newStreamReader;
					return prevValue;
				}
				return newStreamReader;
			}
			return outStreamReader;
		}

		public static volatile StreamWriter mOut ~ delete _;
		public static StreamWriter Out
		{
			get
			{
				return OpenStreamWriter(.Out, ref mOut);
			}
		}

		public static volatile StreamWriter mError ~ delete _;
		public static StreamWriter Error
		{
			get
			{
				return OpenStreamWriter(.Error, ref mError);
			}
		}

		public static volatile StreamReader mIn ~ delete _;
		public static StreamReader In
		{
			get
			{
				return OpenStreamReader(.In, ref mIn);
			}
		}

		public static void Write(String line)
		{
			Out.Write(line).IgnoreError();
		}

		public static void Write(String fmt, params Object[] args)
		{
			String str = scope String();
			str.AppendF(fmt, params args);
			Write(str);
		}
		
		public static void Write(Object obj)
		{
			String str = scope String();
			obj.ToString(str);
			Write(str);
		}

		public static void WriteLine(String line)
		{
			//PrintF("Hey!");
			Out.WriteLine(line).IgnoreError();
		}

		public static void WriteLine(StringView fmt, params Object[] args)
		{
			String str = scope String();
			str.AppendF(fmt, params args);
			WriteLine(str);
		}
		
		public static void WriteLine(Object obj)
		{
			String str = scope String();
			obj.ToString(str);
			WriteLine(str);
		}
		
		[Inline]
		public static void WriteLine(){
			Out.Write("\n").IgnoreError();
		}
	}
}
