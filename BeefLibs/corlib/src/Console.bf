using System.Text;
using System.IO;
using System.Threading;

namespace System
{
	public static class Console
	{
		public static Encoding InputEncoding = Encoding.ASCII;
		public static Encoding OutputEncoding = Encoding.ASCII;
		
		private static ConsoleColor mForegroundColor = .White;
		private static ConsoleColor mBackgroundColor = .Black;

		private static readonly ConsoleColor mOriginalForegroundColor = mForegroundColor;
		private static readonly ConsoleColor mOriginalBackgroundColor = mBackgroundColor;

		public static ConsoleColor ForegroundColor
		{
			get { return mForegroundColor; }
			set { mForegroundColor = value; SetColors(); }
		}

		public static ConsoleColor BackgroundColor
		{
			get { return mBackgroundColor; }
			set { mBackgroundColor = value; SetColors(); }
		}
		
		private const uint32 STD_INPUT_HANDLE  = (uint32) - 10;
		private const uint32 STD_OUTPUT_HANDLE = (uint32) - 11;
		private const uint32 STD_ERROR_HANDLE  = (uint32) - 12;

		[Import("kernel32.dll"), CLink]
		private static extern bool SetConsoleTextAttribute(void* hConsoleOutput, uint16 wAttributes);

		[Import("kernel32.dll"), CLink]
		private static extern void* GetStdHandle(uint32 nStdHandle);
		
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
			String str = scope String(256);
			str.AppendF(fmt, params args);
			Write(str);
		}
		
		public static void Write(Object obj)
		{
			String str = scope String(256);
			obj.ToString(str);
			Write(str);
		}

		public static void WriteLine()
		{
			Out.Write("\n").IgnoreError();
		}

		public static void WriteLine(String line)
		{
			Out.WriteLine(line).IgnoreError();
		}

		public static void WriteLine(StringView fmt, params Object[] args)
		{
			String str = scope String(256);
			str.AppendF(fmt, params args);
			WriteLine(str);
		}
		
		public static void WriteLine(Object obj)
		{
			String str = scope String(256);
			obj.ToString(str);
			WriteLine(str);
		}
		
		public static void ResetColor()
		{
			mForegroundColor = mOriginalForegroundColor;
			mBackgroundColor = mOriginalBackgroundColor;

#if !BF_PLATFORM_WINDOWS
			Write("\x1B[0m");
#endif
		}

		private static void SetColors()
		{
#if BF_PLATFORM_WINDOWS
			let handle = GetStdHandle(STD_OUTPUT_HANDLE);
			let fgColor = ForegroundColor.ToConsoleTextAttribute();
			let bgColor = BackgroundColor.ToConsoleTextAttribute();
			SetConsoleTextAttribute(handle, bgColor * 16 + fgColor);
#else
			Write("\x1B[{}m", ForegroundColor.ToAnsi());
			Write("\x1B[{}m", BackgroundColor.ToAnsi() + 10);
#endif
		}
	}
}
