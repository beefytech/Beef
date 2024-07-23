#pragma warning disable 168
using Beefy.geom;
using System;
using Beefy.widgets;
using System.Security.Cryptography;
using System.Diagnostics;
using System.IO;
using System.Threading;
namespace IDE.util;

class ConsoleProvider
{
	public enum UpdateState
	{
		None,
		Dirty
	}

	public struct Cell
	{
		public char16 mChar;
		public uint16 mAttributes;
	}

	public virtual int Width => 0;
	public virtual int Height => 0;
	public virtual int BufferHeight => 0;
	public virtual bool Attached => false;
	public virtual bool Connected => Attached;
	public virtual int ScrollTop => 0;
	public virtual bool CursorVisible => true;
	public virtual float CursorHeight => 1.0f;

	public virtual (int32 col, int32 row) CursorPos
	{
		get
		{
			return default;
		}

		set
		{

		}
	}

	public virtual Cell GetCell(int col, int row)
	{
		return default;
	}

	public Cell GetCell(int idx)
	{
		int width = Math.Max(Width, 1);
		int scrollTop = ScrollTop;
		return GetCell(idx % width, idx / width - scrollTop);
	}

	public virtual void Resize(int cols, int rows, bool resizeContent)
	{

	}


	public virtual void ScrollTo(int row)
	{

	}

	public virtual void Attach()
	{

	}

	public virtual void Detach()
	{

	}

	public virtual void MouseDown(int col, int row, int btnState, int btnCount, KeyFlags keyFlags)
	{

	}

	public virtual void MouseMove(int col, int row, int btnState, KeyFlags keyFlags)
	{

	}

	public virtual void MouseUp(int col, int row, int btnState, KeyFlags keyFlags)
	{

	}

	public virtual void MouseWheel(int col, int row, int dy)
	{

	}

	public virtual void KeyDown(KeyCode keyCode, KeyFlags keyFlags)
	{

	}

	public virtual void KeyUp(KeyCode keyCode)
	{

	}

	public virtual void SendInput(StringView str)
	{

	}

	public virtual UpdateState Update(bool paused) => .None;

	public virtual uint32 GetColor(int i) => 0xFF000000;
}

class WinNativeConsoleProvider : ConsoleProvider
{
	[CRepr]
	struct CONSOLE_SCREEN_BUFFER_INFOEX
	{
		public uint32 mSize;
		public int16 mWidth;
		public int16 mHeight;
		public uint16 mCursorX;
		public uint16 mCursorY;
		public uint16 wAttributes;
		public RECT mWindowRect;
		public POINT mMaximumWindowSize;
		public uint16 mPopupAttributes;
		public Windows.IntBool mFullscreenSupported;
		public uint32[16] mColorTable;

		public this()
		{
			this = default;
			mSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);
		}
	}

	[CRepr]
	struct POINT : this(int16 x, int16 y)
	{
	}

	[CRepr]
	struct RECT : this(int16 left, int16 top, int16 right, int16 bottom)
	{
		public int16 Width => right - left;
		public int16 Height => bottom - top;
	}

	[CRepr]
	struct CHAR_INFO
	{
		public char16 mChar;
		public uint16 mAttributes;
	}

	[CRepr]
	struct CONSOLE_FONT_INFO
	{
		public uint32 mNumFont;
		public POINT mSize;
	}

	[CRepr]
	struct CONSOLE_CURSOR_INFO
	{
		public uint32 mSize;
		public uint32 mVisible;
	}

	[CRepr]
	struct CONSOLE_SELECTION_INFO
	{
		public uint32 mFlags;
		public POINT mSelectionAnchor;
		public RECT mSelection;
	}

	[CRepr]
	struct KEY_EVENT_RECORD
	{
		public int32 mKeyDown;
		public uint16 mRepeatCount;
		public uint16 mVirtualKeyCode;
		public uint16 mVirtualScanCode;
		public char16 mChar;
		public uint32 mControlKeyState;
	}

	[CRepr]
	struct MOUSE_EVENT_RECORD
	{
		public POINT mMousePosition;
		public uint32 mButtonState;
		public uint32 mControlKeyState;
		public uint32 mEventFlags;
	}

	[CRepr]
	struct INPUT_RECORD
	{
		public uint16 mEventType;
		public INPUT_RECORD_DATA mEventData;
	}

	[Union]
	struct INPUT_RECORD_DATA
	{
		public KEY_EVENT_RECORD mKeyEvent;
		public MOUSE_EVENT_RECORD mMouseEvent;
	}

	class ScreenInfo
	{
		public CONSOLE_SCREEN_BUFFER_INFOEX mInfo;
		public CONSOLE_CURSOR_INFO mCursorInfo;
		public CONSOLE_SELECTION_INFO mSelectionInfo;
		public int32 mScrollTop;
		public CHAR_INFO* mCharInfo;
		public CHAR_INFO* mFullCharInfo;
		public uint32[16] mColorTable = .(0xFF000000, );

		public int32 WindowWidth => mInfo.mWindowRect.Width;
		public int32 WindowHeight => mInfo.mWindowRect.Height;

		public ~this()
		{
			delete mCharInfo;
			delete mFullCharInfo;
		}

		public int GetHashCode()
		{
			MD5 md5 = scope .();
			md5.Update(.((.)&mInfo, sizeof(CONSOLE_SCREEN_BUFFER_INFOEX)));
			md5.Update(.((.)&mSelectionInfo, sizeof(CONSOLE_SELECTION_INFO)));
			md5.Update(.((.)&mCursorInfo, sizeof(CONSOLE_CURSOR_INFO)));
			if (mCharInfo != null)
				md5.Update(.((.)mCharInfo, (int32)mInfo.mWindowRect.Width * mInfo.mWindowRect.Height * sizeof(CHAR_INFO)));
			var hash = md5.Finish();
			return hash.GetHashCode();
		}
	}

#if BF_PLATFORM_WINDOWS
	[CLink, CallingConvention(.Stdcall)]
	public static extern void AllocConsole();

	[CLink, CallingConvention(.Stdcall)]
	public static extern void AttachConsole(int processId);

	[CLink, CallingConvention(.Stdcall)]
	public static extern void FreeConsole();

	[CLink, CallingConvention(.Stdcall)]
	public static extern Windows.IntBool GetConsoleScreenBufferInfoEx(Windows.Handle handle, ref CONSOLE_SCREEN_BUFFER_INFOEX info);

	[CLink, CallingConvention(.Stdcall)]
	public static extern Windows.IntBool SetConsoleScreenBufferInfoEx(Windows.Handle handle, ref CONSOLE_SCREEN_BUFFER_INFOEX info);

	[CLink]
	public static extern Windows.IntBool ReadConsoleOutputW(Windows.Handle handle, void* buffer, POINT bufferSize, POINT bufferCoord, ref RECT readRegion);

	[CLink]
	public static extern Windows.IntBool SetConsoleScreenBufferSize(Windows.Handle handle, POINT bufferSize);

	[CLink]
	public static extern Windows.IntBool SetConsoleWindowInfo(Windows.Handle handle, Windows.IntBool absolute, in RECT window);

	[CLink]
	public static extern Windows.HWnd GetConsoleWindow();

	[CLink]
	public static extern Windows.IntBool GetCurrentConsoleFont(Windows.Handle handle, Windows.IntBool maxWindow, out CONSOLE_FONT_INFO fontInfo);

	[CLink]
	public static extern Windows.IntBool GetConsoleCursorInfo(Windows.Handle handle, out CONSOLE_CURSOR_INFO cursorInfo);

	[CLink]
	public static extern Windows.IntBool GetConsoleSelectionInfo(out CONSOLE_SELECTION_INFO selectionInfo);

	[CLink]
	public static extern Windows.IntBool WriteConsoleInputW(Windows.Handle handle, INPUT_RECORD* eventsPtr, int32 eventCount, out int32 numEventsWritten);

	[CLink]
	public static extern Windows.IntBool ReadConsoleInputW(Windows.Handle handle, INPUT_RECORD* eventsPtr, int32 eventCount, out int32 numEventsRead);
#endif

	ScreenInfo mScreenInfo ~ delete _;
	public bool mDirty;
	bool mHasConsole;
	SpawnedProcess mCmdSpawn ~ delete _;
	SpawnedProcess mExecSpawn ~ delete _;
	public int mLastDrawnHashCode;
	public bool mHideNativeConsole = true;
	
	static uint8[256*5] sKeyCharMap = .(0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 0, 0, 0, 0, 0, 0, 
		0, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 0, 0, 0, 0, 0, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 42, 43, 0, 45, 46, 47, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 59, 61, 44, 45, 46, 47, 
		96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 91, 92, 93, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 33, 64, 35, 36, 37, 94, 38, 42, 40, 0, 0, 0, 0, 0, 0, 
		0, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 42, 43, 0, 45, 46, 47, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 58, 43, 60, 95, 62, 63, 
		126, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123, 124, 125, 34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 0, 0, 0, 0, 0, 0, 
		0, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 0, 0, 0, 0, 0, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 42, 43, 0, 45, 46, 47, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 59, 61, 44, 45, 46, 47, 
		96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 91, 92, 93, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 33, 64, 35, 36, 37, 94, 38, 42, 40, 0, 0, 0, 0, 0, 0, 
		0, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 42, 43, 0, 45, 46, 47, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 58, 43, 60, 95, 62, 63, 
		126, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123, 124, 125, 34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 28, 29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);


	public override int Width => (mScreenInfo != null) ? (mScreenInfo.mInfo.mWindowRect.Width) : 0;
	public override int Height => (mScreenInfo != null) ? (mScreenInfo.mInfo.mWindowRect.Height) : 0;
	public override int BufferHeight
	{
		get
		{
			var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFOEX info = default;
			info.mSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);
#if BF_PLATFORM_WINDOWS
			GetConsoleScreenBufferInfoEx(outHandle, ref info);
#endif
			return info.mHeight;
		}
	}
	public override bool Attached => mHasConsole;
	public override int ScrollTop => (mScreenInfo != null) ? mScreenInfo.mScrollTop : 0;
	public override bool CursorVisible => (mScreenInfo != null) ? (mScreenInfo.mCursorInfo.mVisible != 0) : true;
	public override float CursorHeight => (mScreenInfo != null) ? (mScreenInfo.mCursorInfo.mSize / 100.0f) : 0.15f;

	public ~this()
	{
		mCmdSpawn?.Kill();
		mExecSpawn?.Kill();
	}


	public override (int32 col, int32 row) CursorPos
	{
		get
		{
			if (mScreenInfo != null)
				return (mScreenInfo.mInfo.mCursorX, mScreenInfo.mInfo.mCursorY);
			return default;
		}

		set
		{

		}
	}

	public override Cell GetCell(int col, int row)
	{
		if ((row < 0) || (row >= mScreenInfo.mInfo.mWindowRect.Height))
		{
			GetFullScreenInfo(mScreenInfo);
		}

		if (mScreenInfo.mFullCharInfo != null)
		{
			int bufRow = row + mScreenInfo.mScrollTop;
			if (col >= mScreenInfo.mInfo.mWidth)
				return default;
			if (bufRow >= mScreenInfo.mInfo.mHeight)
				return default;

			var info = mScreenInfo.mFullCharInfo[bufRow * mScreenInfo.mInfo.mWidth + col];
			return .() { mChar = info.mChar, mAttributes = info.mAttributes };
		}

		var info = mScreenInfo.mCharInfo[row * mScreenInfo.mInfo.mWindowRect.Width + col];
		return .() { mChar = info.mChar, mAttributes = info.mAttributes };
	}

	public bool GetScreenInfo(ScreenInfo screenInfo)
	{
		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);

		CONSOLE_SCREEN_BUFFER_INFOEX info = default;
		info.mSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);

#if BF_PLATFORM_WINDOWS
		if (!GetConsoleScreenBufferInfoEx(outHandle, ref info))
			return false;
#endif
		info.mWindowRect.right++;
		info.mWindowRect.bottom++;
		screenInfo.mInfo = info;

		screenInfo.mScrollTop = info.mWindowRect.top;

		//mScrollableWidget.VertScrollTo(screenInfo.mInfo.mWindowRect.top * mCellHeight);

		//int width = info.mWindowRect.Width;
		//int height = info.mWindowRect.Height;

		POINT bufferSize = .(info.mWindowRect.Width, info.mWindowRect.Height);
		screenInfo.mCharInfo = new .[(int32)info.mWindowRect.Width * info.mWindowRect.Height]*;
		RECT readRegion = .(screenInfo.mInfo.mWindowRect.left, (.)screenInfo.mScrollTop, screenInfo.mInfo.mWindowRect.right, (.)(screenInfo.mScrollTop + screenInfo.mInfo.mWindowRect.Height - 1));
#if BF_PLATFORM_WINDOWS
		ReadConsoleOutputW(outHandle, screenInfo.mCharInfo, bufferSize, POINT(0, 0), ref readRegion);

		GetConsoleCursorInfo(outHandle, out screenInfo.mCursorInfo);
		GetConsoleSelectionInfo(out screenInfo.mSelectionInfo);
#endif
		for (int i < 16)
		{
			screenInfo.mColorTable[i] = 0xFF000000 |
				((screenInfo.mInfo.mColorTable[i] >> 16) & 0x0000FF) |
				((screenInfo.mInfo.mColorTable[i]      ) & 0x00FF00) |
				((screenInfo.mInfo.mColorTable[i] << 16) & 0xFF0000);
		}

		return true;
	}

	public bool GetFullScreenInfo(ScreenInfo screenInfo)
	{
		if (screenInfo.mFullCharInfo != null)
			return true;

		if (screenInfo.mCharInfo == null)
		{
			if (!GetScreenInfo(screenInfo))
				return false;
		}

		DeleteAndNullify!(screenInfo.mCharInfo);

		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
		POINT bufferSize = .(screenInfo.mInfo.mWidth, screenInfo.mInfo.mHeight);
		screenInfo.mFullCharInfo = new .[(int32)screenInfo.mInfo.mWidth * screenInfo.mInfo.mHeight]*;
		RECT readRegion = .(0, 0, screenInfo.mInfo.mWidth, screenInfo.mInfo.mHeight);
#if BF_PLATFORM_WINDOWS
		ReadConsoleOutputW(outHandle, screenInfo.mFullCharInfo, bufferSize, POINT(0, 0), ref readRegion);
#endif

		return true;
	}

	public bool UpdateScreenInfo(ScreenInfo screenInfo)
	{
		if (screenInfo.mFullCharInfo == null)
		{
			if (!GetFullScreenInfo(screenInfo))
				return false;
		}

		Internal.MemCpy(screenInfo.mCharInfo,
			screenInfo.mFullCharInfo + screenInfo.mScrollTop * screenInfo.mInfo.mWidth,
			screenInfo.mInfo.mWindowRect.Width * screenInfo.mInfo.mWindowRect.Height * sizeof(CHAR_INFO));
		return true;
	}

	public override void ScrollTo(int row)
	{
		if (mScreenInfo == null)
			return;

		GetFullScreenInfo(mScreenInfo);

		mScreenInfo.mScrollTop = (.)row;
		int windowHeight = mScreenInfo.mInfo.mWindowRect.Height;
		mScreenInfo.mInfo.mWindowRect.top = (.)mScreenInfo.mScrollTop;
		mScreenInfo.mInfo.mWindowRect.bottom = (.)(mScreenInfo.mScrollTop + windowHeight);
	}

	public override void Resize(int cols, int rows, bool resizeContent)
	{
		if (resizeContent)
		{
			if (mScreenInfo != null)
			{
				GetFullScreenInfo(mScreenInfo);
				mScreenInfo.mInfo.mWindowRect.right = (.)(mScreenInfo.mInfo.mWindowRect.left + cols);
				mScreenInfo.mInfo.mWindowRect.bottom = (.)(mScreenInfo.mInfo.mWindowRect.top + rows);
			}
		}

		if (!mHasConsole)
			return;

		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFOEX info = default;
		info.mSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);
#if BF_PLATFORM_WINDOWS
		GetConsoleScreenBufferInfoEx(outHandle, ref info);
#endif

		info.mWindowRect.right = (.)(info.mWindowRect.left + cols);
		info.mWindowRect.bottom = (.)(info.mWindowRect.top + rows);
		//SetConsoleScreenBufferInfoEx(outHandle, ref info);

		//SetConsoleScreenBufferSize(outHandle, .((.)cols, (.)rows));

		//SetConsoleWindowInfo(outHandle, true, info.mWindowRect);

#if BF_PLATFORM_WINDOWS
		GetCurrentConsoleFont(outHandle, false, var fontInfo);

		var window = GetConsoleWindow();

		uint32 style = (.)Windows.GetWindowLong(window, Windows.GWL_STYLE);
		uint32 styleEx = (.)Windows.GetWindowLong(window, Windows.GWL_EXSTYLE);

		Windows.Rect rect = .(0, 0, (.)(cols * fontInfo.mSize.x), (.)(rows * fontInfo.mSize.y));
		Windows.AdjustWindowRectEx(ref rect, style, false, styleEx);

		Windows.SetWindowPos(window, default, 0, 0, rect.Width, rect.Height,
			0x10 /* SWP_NOACTIVATE */
			//0x90 /* SWP_HIDEWINDOW | SWP_NOACTIVATE */
			);
#endif
	}

	public override void Attach()
	{
		if (mHasConsole)
			return;

		mHasConsole = true;

#if BF_PLATFORM_WINDOWS
		//AllocConsole();

		/*ProcessStartInfo procInfo = scope ProcessStartInfo();
		procInfo.UseShellExecute = false;
		procInfo.SetFileName(scope $"{gApp.mInstallDir}/BeefCon_d.exe");
		procInfo.SetArguments(scope $"{Process.CurrentId}");

		String resultStr = scope String();
		mCmdSpawn = new SpawnedProcess();
		mCmdSpawn.Start(procInfo);

		Thread.Sleep(2000);

		var processId = mCmdSpawn.ProcessId;
		if (processId > 0)
			AttachConsole(processId);
		else*/
			AllocConsole();

		var window = GetConsoleWindow();

		if (mHideNativeConsole)
			Windows.SetWindowPos(window, default, 0, 0, 0, 0, 0x290 /* SWP_NOACTIVATE | SWP_NOREPOSITION | SWP_HIDEWINDOW */);

		//ResizeComponents();
#endif
	}

	public override void Detach()
	{
		if (!mHasConsole)
			return;

		if (mScreenInfo != null)
			GetFullScreenInfo(mScreenInfo);

		mHasConsole = false;

#if BF_PLATFORM_WINDOWS
		FreeConsole();
#endif

		mCmdSpawn?.Kill();
		DeleteAndNullify!(mCmdSpawn);
		mExecSpawn?.Kill();
		DeleteAndNullify!(mExecSpawn);
	}

	public uint32 GetControlKeyState(KeyFlags keyFlags)
	{
		uint16 controlKeyState = 0;
		if (keyFlags.HasFlag(.Alt))
			controlKeyState |= 1;
		if (keyFlags.HasFlag(.Ctrl))
			controlKeyState |= 4;
		if (keyFlags.HasFlag(.Shift))
			controlKeyState |= 0x10;
		if (keyFlags.HasFlag(.CapsLock))
			controlKeyState |= 0x80;
		return controlKeyState;
	}

	public override void MouseDown(int col, int row, int btnState, int btnCount, KeyFlags keyFlags)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		INPUT_RECORD input = default;
		input.mEventType = 2 /*MOUSE_EVENT */;
		input.mEventData.mMouseEvent.mButtonState = (.)btnState;
		if (btnCount > 1)
			input.mEventData.mMouseEvent.mEventFlags |= 2;
		input.mEventData.mMouseEvent.mMousePosition = .((.)col, (.)row);
		input.mEventData.mMouseEvent.mControlKeyState = GetControlKeyState(keyFlags);
		WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
	}

	public override void MouseMove(int col, int row, int btnState, KeyFlags keyFlags)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		INPUT_RECORD input = default;
		input.mEventType = 2 /*MOUSE_EVENT */;
		input.mEventData.mMouseEvent.mEventFlags |= 1; /* MOUSE_MOVED */
		input.mEventData.mMouseEvent.mButtonState = (.)btnState;
		input.mEventData.mMouseEvent.mMousePosition = .((.)col, (.)row);
		input.mEventData.mMouseEvent.mControlKeyState = GetControlKeyState(keyFlags);
		WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
	}

	public override void MouseUp(int col, int row, int btnState, KeyFlags keyFlags)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		INPUT_RECORD input = default;
		input.mEventType = 2 /*MOUSE_EVENT */;
		input.mEventData.mMouseEvent.mButtonState = (.)btnState;
		input.mEventData.mMouseEvent.mMousePosition = .((.)col, (.)row);
		input.mEventData.mMouseEvent.mControlKeyState = GetControlKeyState(keyFlags);
		WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
	}

	public override void MouseWheel(int col, int row, int dy)
	{
		var window = GetConsoleWindow();
		Windows.SendMessageW(window, 0x0007, 0, 0); // WM_SETFOCUS
		//Windows.SendMessageW(window, 0x0006, 0, 0); // WM_ACTIVATE

		float x = col;
		float y = row;

		Windows.SendMessageW(window, 0x0200 /*WM_MOUSEMOVE*/, 0, (int)x | ((int)y << 16));
		Windows.SendMessageW(window, 0x020A /*WM_MOUSEWHEEL*/, (int32)(120 * dy) << 16, (int)x | ((int)y << 16));
	}

	public override void KeyDown(KeyCode keyCode, KeyFlags keyFlags)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		INPUT_RECORD input = default;

		/*if (keyEvent.mKeyCode == .F1)
		{
			Debug.WriteLine("Key Events:");
			while (true)
			{
				ReadConsoleInputW(inHandle, &input, 1, var numEventsRead);

				if (input.mEventType == 1)
				{
					if (input.mEventData.mKeyEvent.mChar != 0)
					{
						int keyMod = default;
						if ((input.mEventData.mKeyEvent.mControlKeyState & 8) != 0) // Ctrl
						{
							keyMod |= 4;
						}
						else
						{
							if ((input.mEventData.mKeyEvent.mControlKeyState & 0x10) != 0) // Shift
								keyMod |= 1;
							if ((input.mEventData.mKeyEvent.mControlKeyState & 0x80) != 0) // Caps Lock
								keyMod |= 2;
						}
						
						/*if ((input.mEventData.mKeyEvent.mControlKeyState & 2) != 0) // Alt
							flags |= .Alt;*/
						
						Debug.WriteLine($"{input.mEventData.mKeyEvent.mVirtualKeyCode} {keyMod} : {(int)input.mEventData.mKeyEvent.mChar} {input.mEventData.mKeyEvent.mChar}");

						uint16 keyState = ((uint16)keyMod << 8) + (uint16)input.mEventData.mKeyEvent.mVirtualKeyCode;
						sKeyCharMap[keyState] = (uint8)input.mEventData.mKeyEvent.mChar;
					}

					if (input.mEventData.mKeyEvent.mChar == '?')
					{
						for (int i < sKeyCharMap.Count)
						{
							if (i % 64 == 0)
								Debug.WriteLine();
							Debug.Write($"{sKeyCharMap[i]}, ");
						}
						Debug.WriteLine();
					}
				}
				else if (input.mEventType == 2)
				{
					
				}
			}
			return;
		}*/

		input.mEventType = 1 /*KEY_EVENT */;
		input.mEventData.mKeyEvent.mKeyDown = 1;
		input.mEventData.mKeyEvent.mRepeatCount = 1;
		input.mEventData.mKeyEvent.mVirtualKeyCode = (.)keyCode;
		//input.mEventData.mKeyEvent.mVirtualScanCode = 61;

		int keyMod = 0;
		if (keyFlags.HasFlag(.Ctrl))
		{
			keyMod |= 4;
		}
		else
		{
			if (keyFlags.HasFlag(.Shift))
				keyMod |= 1;
			if (keyFlags.HasFlag(.CapsLock))
				keyMod |= 2;
		}

		input.mEventData.mKeyEvent.mControlKeyState = GetControlKeyState(keyFlags);
		input.mEventData.mKeyEvent.mChar = (.)sKeyCharMap[(keyMod << 8) | (int)keyCode];

		WriteConsoleInputW(inHandle, &input, 1, var numEventsWritten);
	}

	public override void KeyUp(KeyCode keyCode)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		INPUT_RECORD input = default;
		input.mEventType = 1 /*KEY_EVENT */;
		input.mEventData.mKeyEvent.mVirtualKeyCode = (.)keyCode;
		WriteConsoleInputW(inHandle, &input, 1, var numEventsWritten);
	}

	public override void SendInput(StringView str)
	{
		var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
		for (var c in str.DecodedChars)
		{
			INPUT_RECORD input = default;
			input.mEventType = 1 /*KEY_EVENT */;
			input.mEventData.mKeyEvent.mKeyDown = 1;
			input.mEventData.mKeyEvent.mRepeatCount = 1;
			input.mEventData.mKeyEvent.mChar = (.)c;
			WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
		}
	}

	public override UpdateState Update(bool paused)
	{
		if (paused)
			return .None;

		ScreenInfo newScreenInfo = new .();
		if (GetScreenInfo(newScreenInfo))
		{
			delete mScreenInfo;
			mScreenInfo = newScreenInfo;
		}
		else
		{
			Detach();
			delete newScreenInfo;
		}

		int hashCode = (mScreenInfo?.GetHashCode()).GetValueOrDefault();

		if (hashCode != mLastDrawnHashCode)
		{
			mLastDrawnHashCode = hashCode;
			mDirty = true;
		}

		if (mDirty)
		{
			mDirty = false;
			return .Dirty;
		}
		return .None;
	}

	public override uint32 GetColor(int i)
	{
		return (mScreenInfo != null) ? mScreenInfo.mColorTable[i] : 0xFF000000;
	}
}

class BeefConConsoleProvider : ConsoleProvider
{
	public enum Message
	{
		None,
		GetData,
		Resize,
		Data,
		InputString,
		KeyDown,
		KeyUp,
		MouseDown,
		MouseMove,
		MouseUp,
		MouseWheel,
		ScrollTo,
		Update
	}

	public class Pipe
	{
		public NamedPipe mSendPipe = new .() ~ delete _;
		public NamedPipe mRecvPipe = new .() ~ delete _;
		public MemoryStream mRecvStream = new .() ~ delete _;
		public MemoryStream mSendStream = new .() ~ delete _;
		public MemoryStream Stream => mSendStream;
		public bool mFailed;
		public bool mConnected;
		public Thread mThread ~ delete _;
		public Monitor mDataMonitor = new .() ~ delete _;
		public int mPendingReadClear;
		public bool mExiting;

		public this()
		{

		}

		public ~this()
		{
			mExiting = true;
			using (mDataMonitor.Enter())
			{
				mSendPipe.Close();
				mRecvPipe.Close();
			}
			if (mThread != null)
			{
				mThread.Join();
			}
		}

		void ThreadProc()
		{
			while (!mExiting)
			{
				uint8[4096] data = ?;

				using (mDataMonitor.Enter())
				{
					if (mExiting)
						return;
				}

				switch (mRecvPipe.TryRead(.(&data, 4096), -1))
				{
				case .Ok(int len):
					if (len == 0)
					{
						mFailed = true;
						return;
					}
					mConnected = true;
					using (mDataMonitor.Enter())
					{
						mRecvStream.TryWrite(.(&data, len));
					}
				case .Err(let err):
					if ((err == .PipeListening) && (!mConnected))
					{
						Thread.Sleep(20);
						break;
					}	
					mFailed = true;
					return;
				}
			}
		}

		void StartThread()
		{
			mThread = new Thread(new => ThreadProc);
			mThread.Start(false);
		}

		public Result<void> Connect(int processId, int conId)
		{
			if (mSendPipe.Open(".", scope $"BEEFCON_{processId}_{conId}_A", .None) case .Err)
				return .Err;
			if (mRecvPipe.Open(".", scope $"BEEFCON_{processId}_{conId}_B", .None) case .Err)
				return .Err;
			StartThread();
			mConnected = true;
			return .Ok;
		}

		public Result<void> Listen(int processId, int conId)
		{
			if (mRecvPipe.Create(".", scope $"BEEFCON_{processId}_{conId}_A", .None) case .Err)
				return .Err;
			if (mSendPipe.Create(".", scope $"BEEFCON_{processId}_{conId}_B", .None) case .Err)
				return .Err;
			StartThread();
			return .Ok;
		}

		public void StartMessage(Message message)
		{
			Debug.Assert(Stream.Length == 0);
			mSendStream.Write(message);
		}

		public void EndMessage()
		{
			defer mSendStream.Clear();

			if (!mConnected)
				return;

			if (mSendPipe.Write((int32)mSendStream.Length) case .Err)
			{
				mFailed = true;
				return;
			}

			Span<uint8> span = .(mSendStream.Memory.Ptr, mSendStream.Length);
			while (span.Length > 0)
			{
				switch (mSendPipe.TryWrite(span))
				{
				case .Ok(int len):
					span.RemoveFromStart(len);
				case .Err:
					mFailed = true;
					return;
				}
			}
		}

		public Result<Span<uint8>> ReadMessage(int timeoutMS)
		{
			using (mDataMonitor.Enter())
			{
				if (mPendingReadClear > 0)
				{
					mRecvStream.Memory.RemoveRange(0, mPendingReadClear);
					mRecvStream.Position = mRecvStream.Position - mPendingReadClear;
					mPendingReadClear = 0;
				}

				if (mRecvStream.Length < 4)
					return .Err;

				int wantTotalLen = *(int32*)mRecvStream.Memory.Ptr + 4;
				if (mRecvStream.Length < wantTotalLen)
					return .Err;

				mPendingReadClear = wantTotalLen;
				return .Ok(.(mRecvStream.Memory.Ptr + 4, wantTotalLen - 4));
			}
		}
	}

	public static int sConId = 0;

	public SpawnedProcess mBeefConProcess ~ delete _;
	public int mConId = ++sConId;
	public String mWorkingDir = new .() ~ delete _;
	public String mBeefConExePath ~ delete _;
	public String mTerminalExe ~ delete _;
	public Pipe mPipe = new .() ~ delete _;
	public int mProcessId = Process.CurrentId;
	public bool mAttached;

	public int32 mResizedWidth;
	public int32 mResizedHeight;
	public int32 mWidth;
	public int32 mHeight;
	public int32 mBufferHeight;
	public int32 mScrollTop;
	public bool mCursorVisible;
	public float mCursorHeight;
	public Cell* mCells ~ delete _;
	public (int32 col, int32 row) mCursorPos;
	public uint32[16] mColors;
	public int mLastDrawnHashCode;
	public bool mDirty;
	public int32 mFailDelay;

	public override bool Connected => mPipe.mConnected && !mPipe.mFailed;
	public override int Width => mWidth;
	public override int Height => mHeight;
	public override int BufferHeight => mBufferHeight;
	public override bool Attached => mAttached;
	public override int ScrollTop => mScrollTop;
	public override bool CursorVisible => mCursorVisible;
	public override float CursorHeight => mCursorHeight;
	public override (int32 col, int32 row) CursorPos
	{
		get
		{
			return mCursorPos;
		}

		set
		{
			mCursorPos = value;
		}
	}

	public override Cell GetCell(int col, int row)
	{
		if (mCells == null)
			return default;
		return mCells[row * mWidth + col];
	}

	public override uint32 GetColor(int i)
	{
		return mColors[i];
	}

	public override void Attach()
	{
		if (mAttached)
			return;

		if (mBeefConProcess != null)
		{
			mBeefConProcess.Kill();
			DeleteAndNullify!(mBeefConProcess);
		}

		if (mBeefConExePath != null)
		{
			ProcessStartInfo procInfo = scope ProcessStartInfo();
			procInfo.UseShellExecute = false;
			procInfo.SetFileName(mBeefConExePath);
			procInfo.SetWorkingDirectory(mWorkingDir);
			procInfo.SetArguments(scope $"{Process.CurrentId} {sConId} {mTerminalExe}");

			String resultStr = scope String();
			mBeefConProcess = new SpawnedProcess();
			mBeefConProcess.Start(procInfo).IgnoreError();
		}

		mDirty = true;
		mAttached = true;
	}

	public override void Detach()
	{
		if (!mAttached)
			return;

		mWidth = 0;
		mHeight = 0;
		mAttached = false;

		DeleteAndNullify!(mPipe);
		mPipe = new .();

		if (mBeefConProcess != null)
		{
			delete mBeefConProcess;
			mBeefConProcess = null;
		}
	}

	static mixin GET<T>(var ptr)
	{
		*((T*)(ptr += sizeof(T)) - 1)
	}

	public override UpdateState Update(bool paused)
	{
		if (!mAttached)
			return .None;

		if (mPipe.mFailed)
		{
			if (mFailDelay == 0)
			{
				mFailDelay = 120;
				return .None;
			}
			else
			{
				if (--mFailDelay == 0)
				{
					Detach();
					Attach();
				}
				return .None;
			}
		}

		if (!mPipe.mConnected)
		{
			if (mPipe.Connect((mBeefConProcess != null) ? mProcessId : 123, mConId) case .Err)
				return .None;
			Resize(mResizedWidth, mResizedHeight, false);
		}

		if (!paused)
		{
			mPipe.StartMessage(.Update);
			mPipe.Stream.Write(paused);
			mPipe.EndMessage();
		}

		mPipe.StartMessage(.GetData);
		mPipe.EndMessage();

		MessageLoop: while (true)
		{
			switch (mPipe.ReadMessage(0))
			{
			case .Ok(let msg):
				uint8* ptr = msg.Ptr + 1;
				switch (*(Message*)msg.Ptr)
				{
				case .Data:
					mWidth = GET!<int32>(ptr);
					mHeight = GET!<int32>(ptr);
					mBufferHeight = GET!<int32>(ptr);
					mScrollTop = GET!<int32>(ptr);
					mCursorVisible = GET!<bool>(ptr);
					mCursorHeight = GET!<float>(ptr);
					mCursorPos = GET!<(int32 col, int32 row)>(ptr);
					for (int i < 16)
					{
						uint32 color = GET!<uint32>(ptr);
						mColors[i] = 0xFF000000 |
							((color >> 16) & 0x0000FF) |
							((color      ) & 0x00FF00) |
							((color << 16) & 0xFF0000);
						mColors[i] = color;
					}
					delete mCells;
					mCells = new Cell[mWidth * mHeight]*;
					for (int row < mHeight)
					{
						for (int col < mWidth)
						{
							mCells[row * mWidth + col] = GET!<Cell>(ptr);
						}
					}

					int hashCode = MD5.Hash(msg).GetHashCode();
					if (hashCode != mLastDrawnHashCode)
					{
						mLastDrawnHashCode = hashCode;
						mDirty = true;
					}
				default:
				}
			case .Err:
				break MessageLoop;
			}
		}

		if (mDirty)
		{
			mDirty = false;
			return .Dirty;
		}
		return .None;
	}

	public override void Resize(int cols, int rows, bool resizeContent)
	{
		mResizedWidth = (.)cols;
		mResizedHeight = (.)rows;
		
		mPipe.StartMessage(.Resize);
		mPipe.Stream.Write((int32)cols);
		mPipe.Stream.Write((int32)rows);
		mPipe.Stream.Write(resizeContent);
		mPipe.EndMessage();
	}

	public override void ScrollTo(int row)
	{
		if (row == mScrollTop)
			return;
		mScrollTop = (.)row;
		mPipe.StartMessage(.ScrollTo);
		mPipe.Stream.Write((int32)row);
		mPipe.EndMessage();
	}

	public override void KeyDown(KeyCode keyCode, KeyFlags keyFlags)
	{
		mPipe.StartMessage(.KeyDown);
		mPipe.Stream.Write(keyCode);
		mPipe.Stream.Write(keyFlags);
		mPipe.EndMessage();
	}

	public override void KeyUp(KeyCode keyCode)
	{
		mPipe.StartMessage(.KeyUp);
		mPipe.Stream.Write(keyCode);
		mPipe.EndMessage();
	}

	public override void SendInput(StringView str)
	{
		mPipe.StartMessage(.InputString);
		mPipe.Stream.WriteStrSized32(str);
		mPipe.EndMessage();
	}

	public override void MouseDown(int col, int row, int btnState, int btnCount, KeyFlags keyFlags)
	{
		mPipe.StartMessage(.MouseDown);
		mPipe.Stream.Write((int32)col);
		mPipe.Stream.Write((int32)row);
		mPipe.Stream.Write((int32)btnState);
		mPipe.Stream.Write((int32)btnCount);
		mPipe.Stream.Write(keyFlags);
		mPipe.EndMessage();
	}

	public override void MouseMove(int col, int row, int btnState, KeyFlags keyFlags)
	{
		mPipe.StartMessage(.MouseMove);
		mPipe.Stream.Write((int32)col);
		mPipe.Stream.Write((int32)row);
		mPipe.Stream.Write((int32)btnState);
		mPipe.Stream.Write(keyFlags);
		mPipe.EndMessage();
	}

	public override void MouseUp(int col, int row, int btnState, KeyFlags keyFlags)
	{
		mPipe.StartMessage(.MouseUp);
		mPipe.Stream.Write((int32)col);
		mPipe.Stream.Write((int32)row);
		mPipe.Stream.Write((int32)btnState);
		mPipe.Stream.Write(keyFlags);
		mPipe.EndMessage();
	}

	public override void MouseWheel(int col, int row, int dy)
	{
		mPipe.StartMessage(.MouseWheel);
		mPipe.Stream.Write((int32)col);
		mPipe.Stream.Write((int32)row);
		mPipe.Stream.Write((int32)dy);
		mPipe.EndMessage();
	}
}