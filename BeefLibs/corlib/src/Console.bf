using System.Text;
using System.IO;
using System.Threading;

namespace System
{
	public static class Console
	{
		static Encoding InputEncoding = Encoding.ASCII;
		static Encoding OutputEncoding = Encoding.ASCII;
		
		static ConsoleColor sForegroundColor = .White;
		static ConsoleColor sBackgroundColor = .Black;

		static readonly ConsoleColor sOriginalForegroundColor = sForegroundColor;
		static readonly ConsoleColor sOriginalBackgroundColor = sBackgroundColor;

		public static ConsoleColor ForegroundColor
		{
			get { return sForegroundColor; }
			set { sForegroundColor = value; SetColors(); }
		}

		public static ConsoleColor BackgroundColor
		{
			get { return sBackgroundColor; }
			set { sBackgroundColor = value; SetColors(); }
		}
		
		const uint32 STD_INPUT_HANDLE  = (uint32)-10;
		const uint32 STD_OUTPUT_HANDLE = (uint32)-11;
		const uint32 STD_ERROR_HANDLE  = (uint32)-12;

		[CRepr]
		struct CONSOLE_SCREEN_BUFFER_INFO
		{
			public uint16[2] mSize;
			public uint16[2] mCursorPosition;
			public uint16 mAttributes;
			public uint16[4] mWindow;
			public uint16[2] mMaximumWindowSize;
		}

		[CLink]
		static extern int SetConsoleTextAttribute(void* hConsoleOutput, uint16 wAttributes);

		[CLink]
		static extern int GetConsoleScreenBufferInfo(void* hConsoleOutput, out CONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo);

		[CLink]
		static extern void* GetStdHandle(uint32 nStdHandle);

#if BF_PLATFORM_WINDOWS
		public static this()
		{
			let handle = GetStdHandle(STD_OUTPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFO consoleInfo = .();
			if (GetConsoleScreenBufferInfo(handle, out consoleInfo) != 0)
			{
				sOriginalForegroundColor.ConsoleTextAttribute = (uint8)(consoleInfo.mAttributes & 0xF);
				sOriginalBackgroundColor.ConsoleTextAttribute = (uint8)(consoleInfo.mAttributes >> 4);
			}
		}
#endif

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
			sForegroundColor = sOriginalForegroundColor;
			sBackgroundColor = sOriginalBackgroundColor;

#if !BF_PLATFORM_WINDOWS
			Write("\x1B[0m");
#endif
		}

		static void SetColors()
		{
#if BF_PLATFORM_WINDOWS
			let handle = GetStdHandle(STD_OUTPUT_HANDLE);
			let fgColor = ForegroundColor.ConsoleTextAttribute;
			let bgColor = BackgroundColor.ConsoleTextAttribute;
			SetConsoleTextAttribute(handle, bgColor * 16 + fgColor);
#else
			Write("\x1B[{}m", ForegroundColor.ToAnsi());
			Write("\x1B[{}m", BackgroundColor.ToAnsi() + 10);
#endif
		}
	}
}
