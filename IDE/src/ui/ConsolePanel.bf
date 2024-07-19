#pragma warning disable 168

using System;
using Beefy.geom;
using Beefy.gfx;
using System.Text;
using Beefy.theme.dark;
using System.Security.Cryptography;
using Beefy.widgets;
using Beefy.events;
using System.Diagnostics;
using Beefy.utils;
using System.Threading;

namespace IDE.ui;

class ConsolePanel : Panel
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

	class View : Widget
	{
		public ConsolePanel mConsolePanel;

		public this(ConsolePanel ConsolePanel)
		{
			mConsolePanel = ConsolePanel;
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			if (mConsolePanel.mMousePassThrough)
			{				
				var cell = mConsolePanel.GetCell(x, y);
				INPUT_RECORD input = default;
				input.mEventType = 2 /*MOUSE_EVENT */;
				input.mEventData.mMouseEvent.mButtonState = (.)mMouseFlags;
				if (btnCount > 1)
					input.mEventData.mMouseEvent.mEventFlags |= 2;
				input.mEventData.mMouseEvent.mMousePosition = .((.)cell.mX, (.)cell.mY);
				input.mEventData.mMouseEvent.mControlKeyState = mConsolePanel.GetControlKeyState(mWidgetWindow.GetKeyFlags(false));
				WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
			}
			else
			{
				if (btn == 0)
				{

				}
				else if (btn == 1)
				{
					var text = gApp.GetClipboardText(.. scope .());
					for (var c in text.DecodedChars)
					{
						INPUT_RECORD input = default;
						input.mEventType = 1 /*KEY_EVENT */;
						input.mEventData.mKeyEvent.mKeyDown = 1;
						input.mEventData.mKeyEvent.mRepeatCount = 1;
						//input.mEventData.mKeyEvent.mVirtualKeyCode = (.)keyEvent.mKeyCode;
						//input.mEventData.mKeyEvent.mVirtualScanCode = 61;
						//input.mEventData.mKeyEvent.mControlKeyState = GetControlKeyState(keyEvent.mKeyFlags);
						input.mEventData.mKeyEvent.mChar = (.)c;
						WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);
					}
				}
			}

			/*int flags = (.)mMouseFlags;
			var window = GetConsoleWindow();

			//Windows.SendMessageW(window, 0x0006, 0, 0);
			Windows.SendMessageW(window, 0x0007, 0, 0);
			//Windows.SetActiveWindow(window);
			//Windows.SetFocus(window);

			if (btn == 0)
				Windows.SendMessageW(window, 0x0201 /*WM_LBUTTONDOWN*/, flags, (int)x | ((int)y << 16));
			else if (btn == 1)
			{
				Windows.SendMessageW(window, 0x0204 /*WM_RBUTTONDOWN*/, flags, (int)x | ((int)y << 16));

				//Windows.SendMessageW(window, 0x0100, 0, 0);
			}*/
		}

		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			var cell = mConsolePanel.GetCell(x, y);
			INPUT_RECORD input = default;
			input.mEventType = 2 /*MOUSE_EVENT */;
			input.mEventData.mMouseEvent.mButtonState = (.)mMouseFlags;
			input.mEventData.mMouseEvent.mEventFlags |= 1; /* MOUSE_MOVED */
			input.mEventData.mMouseEvent.mMousePosition = .((.)cell.mX, (.)cell.mY);
			input.mEventData.mMouseEvent.mControlKeyState = mConsolePanel.GetControlKeyState(mWidgetWindow.GetKeyFlags(false));
			WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);

			/*var window = GetConsoleWindow();
			Windows.SendMessageW(window, 0x0200 /*WM_MOUSEMOVE*/, 0, (int)x | ((int)y << 16));*/
		}

		public override void MouseUp(float x, float y, int32 btn)
		{
			base.MouseUp(x, y, btn);

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			var cell = mConsolePanel.GetCell(x, y);
			INPUT_RECORD input = default;
			input.mEventType = 2 /*MOUSE_EVENT */;
			input.mEventData.mMouseEvent.mButtonState = (.)mMouseFlags;
			//input.mEventData.mMouseEvent.mEventFlags |= 1; /* MOUSE_MOVED */
			input.mEventData.mMouseEvent.mMousePosition = .((.)cell.mX, (.)cell.mY);
			input.mEventData.mMouseEvent.mControlKeyState = mConsolePanel.GetControlKeyState(mWidgetWindow.GetKeyFlags(false));
			WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);

			/*var window = GetConsoleWindow();
			Windows.SendMessageW(window, 0x0202 /*WM_LBUTTONUP*/, 0, (int)x | ((int)y << 16));*/
		}

		public override void KeyDown(KeyDownEvent keyEvent)
		{
			base.KeyDown(keyEvent);

			if (keyEvent.mKeyCode == .Insert)
			{
				ProcessStartInfo procInfo = scope ProcessStartInfo();
				procInfo.UseShellExecute = false;
				procInfo.SetFileName("Powershell.exe");

				String resultStr = scope String();
				var spawn = scope SpawnedProcess();
				spawn.Start(procInfo);
			}

			if (keyEvent.mKeyCode == .Tilde)
			{
				if (mConsolePanel.mHasConsole)
				{
					mConsolePanel.Detach();
				}
				else
				{
					mConsolePanel.Attach();

					ProcessStartInfo procInfo = scope ProcessStartInfo();
					procInfo.UseShellExecute = false;
					procInfo.SetFileName("Powershell.exe");

					String resultStr = scope String();
					mConsolePanel.mExecSpawn = new SpawnedProcess();
					mConsolePanel.mExecSpawn.Start(procInfo);
				}
			}
		}

		public override void KeyUp(KeyCode keyCode)
		{
			base.KeyUp(keyCode);

			
		}

		public override void GotFocus()
		{
			base.GotFocus();
			mConsolePanel.mCursorBlinkTicks = 0;
		}

		public override void MouseWheel(MouseEvent evt)
		{
			if ((mConsolePanel.mPaused) || (mConsolePanel.mHasConsole))
			{
				base.MouseWheel(evt);
				return;
			}

			/*var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			var cell = mConsolePanel.GetCell(evt.mX, evt.mY);
			INPUT_RECORD input = default;
			input.mEventType = 2 /*MOUSE_EVENT */;
			input.mEventData.mMouseEvent.mButtonState = (.)((int32)mMouseFlags | ((int32)evt.mWheelDeltaY << 16));
			input.mEventData.mMouseEvent.mEventFlags |= 4 /* MOUSE_WHEELED  */;
			input.mEventData.mMouseEvent.mMousePosition = .((.)cell.mX, (.)cell.mY);
			input.mEventData.mMouseEvent.mControlKeyState = mConsolePanel.GetControlKeyState(mWidgetWindow.GetKeyFlags());
			WriteConsoleInputW(inHandle, &input, 1, var numEVentsWritten);*/

			float x = evt.mX;
			float y = evt.mY;

			var window = GetConsoleWindow();
			Windows.SendMessageW(window, 0x0007, 0, 0); // WM_SETFOCUS
			//Windows.SendMessageW(window, 0x0006, 0, 0); // WM_ACTIVATE
			
			
			Windows.SendMessageW(window, 0x0200 /*WM_MOUSEMOVE*/, 0, (int)x | ((int)y << 16));
			Windows.SendMessageW(window, 0x020A /*WM_MOUSEWHEEL*/, (int32)(120 * evt.mWheelDeltaY) << 16, (int)x | ((int)y << 16));
		}
	}

	class ScreenInfo
	{
		public CONSOLE_SCREEN_BUFFER_INFOEX mInfo;
		public CONSOLE_CURSOR_INFO mCursorInfo;
		public CONSOLE_SELECTION_INFO mSelectionInfo;
		public int32 mScrollTop;
		public CHAR_INFO* mCharInfo;
		public CHAR_INFO* mFullCharInfo;

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
			md5.Update(.((.)mCharInfo, (int32)mInfo.mWindowRect.Width * mInfo.mWindowRect.Height * sizeof(CHAR_INFO)));
			var hash = md5.Finish();
			return hash.GetHashCode();
		}
	}

	public int mLastDrawnHashCode;
	public DarkScrollbar mScrollbar;
	public ScrollableWidget mScrollableWidget;
	public int32 mCellWidth;
	public int32 mCellHeight;
	public bool mPaused;
	ScreenInfo mScreenInfo ~ delete _;
	View mView;
	int mCursorBlinkTicks;
	SpawnedProcess mCmdSpawn ~ delete _;
	SpawnedProcess mExecSpawn ~ delete _;
	bool mHasConsole;
	bool mMousePassThrough;
	(POINT start, POINT end)? mSelection;

	public this()
	{
		/*mScrollbar = new DarkScrollbar();
		mScrollbar.mOrientation = .Vert;
		mScrollbar.Init();
		AddWidget(mScrollbar);*/

		mScrollableWidget = new ScrollableWidget();
		mScrollableWidget.InitScrollbars(false, true);
		AddWidget(mScrollableWidget);

		mView = new View(this);
		mView.mAutoFocus = true;
		mScrollableWidget.mScrollContentContainer.AddWidget(mView);
		mScrollableWidget.mScrollContent = mView;

		mScrollableWidget.mVertScrollbar.mOnScrollEvent.Add(new (evt) =>
			{
				mPaused = true;
			});
	}

	public ~this()
	{
		mCmdSpawn?.Kill();
		mExecSpawn?.Kill();
	}

	public override void Serialize(StructuredData data)
	{
		base.Serialize(data);

		data.Add("Type", "ConsolePanel");
	}

	public override void AddedToParent()
	{
		base.AddedToParent();

	}

	public void Attach()
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
		Windows.SetWindowPos(window, default, 0, 0, 0, 0, 0x290 /* SWP_NOACTIVATE | SWP_NOREPOSITION | SWP_HIDEWINDOW */);

		ResizeComponents();
#endif
	}

	public void Detach()
	{
		if (!mHasConsole)
			return;

		mHasConsole = false;

#if BF_PLATFORM_WINDOWS
		FreeConsole();
#endif

		mCmdSpawn?.Kill();
		DeleteAndNullify!(mCmdSpawn);
		mExecSpawn?.Kill();
		DeleteAndNullify!(mExecSpawn);
	}

	public override void Update()
	{
		base.Update();

		if (mScrollableWidget.mVertScrollbar.mThumb.mMouseDown)
			mPaused = true;

		if (!mPaused)
		{
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
		}

		if (mScreenInfo != null)
		{
			if ((mPaused) || (!mHasConsole))
			{
				mScreenInfo.mScrollTop = (.)(mScrollableWidget.mVertScrollbar.mContentPos / mCellHeight);
				
				int windowHeight = mScreenInfo.mInfo.mWindowRect.Height;
				mScreenInfo.mInfo.mWindowRect.top = (.)mScreenInfo.mScrollTop;
				mScreenInfo.mInfo.mWindowRect.bottom = (.)(mScreenInfo.mScrollTop + windowHeight);

				UpdateScreenInfo(mScreenInfo);
			}
		}
		
		//mPaused = false;

		/*if (mUpdatingScrollPos)
		{
			int32 windowHeight = screenInfo.WindowHeight;

			var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);

			screenInfo.mInfo.mWindowRect.top = (.)(mScrollableWidget.mVertScrollbar.mContentPos / mCellHeight);
			screenInfo.mInfo.mWindowRect.bottom = (.)(screenInfo.mInfo.mWindowRect.top + windowHeight);

			var result = SetConsoleScreenBufferInfoEx(outHandle, ref screenInfo.mInfo);

			CONSOLE_SCREEN_BUFFER_INFOEX info = .();
			GetConsoleScreenBufferInfoEx(outHandle, ref info);

			mUpdatingScrollPos = false;
		}*/

		if (mWidgetWindow.IsKeyDown(.Control))
		{
			if (mUpdateCnt % 30 == 0)
			{
				//var window = GetConsoleWindow();
				//Windows.SetWindowPos(window, default, 0, 0, 0, 0, 0x93);
				/*screenInfo.mInfo.mColorTable[7] = 0xCCCCCC;
				screenInfo.mInfo.mWindowRect.top++;
				screenInfo.mInfo.mWindowRect.bottom++;

				//screenInfo.mInfo.mCursorY--;

				var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
				SetConsoleScreenBufferInfoEx(outHandle, ref screenInfo.mInfo);*/
			}
		}

		int hashCode = (mScreenInfo?.GetHashCode()).GetValueOrDefault();

		if (hashCode != mLastDrawnHashCode)
		{
			mLastDrawnHashCode = hashCode;
			MarkDirty();
		}

		//float height = mScreenInfo.mInfo.mHeight * mCellHeight;
		//mScrollableWidget.mScrollContent.Resize(0, 0, 0, height);
		//mScrollableWidget.RehupSize();

		mCursorBlinkTicks++;
		if (mView.mHasFocus)
			MarkDirty();
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

		mScrollableWidget.VertScrollTo(screenInfo.mInfo.mWindowRect.top * mCellHeight);

		int width = info.mWindowRect.Width;
		int height = info.mWindowRect.Height;

		POINT bufferSize = .(info.mWindowRect.Width, info.mWindowRect.Height);
		screenInfo.mCharInfo = new .[(int32)info.mWindowRect.Width * info.mWindowRect.Height]*;
		RECT readRegion = .(screenInfo.mInfo.mWindowRect.left, (.)screenInfo.mScrollTop, screenInfo.mInfo.mWindowRect.right, (.)(screenInfo.mScrollTop + screenInfo.mInfo.mWindowRect.Height - 1));
#if BF_PLATFORM_WINDOWS
		ReadConsoleOutputW(outHandle, screenInfo.mCharInfo, bufferSize, POINT(0, 0), ref readRegion);

		GetConsoleCursorInfo(outHandle, out screenInfo.mCursorInfo);
		GetConsoleSelectionInfo(out screenInfo.mSelectionInfo);
#endif
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

	public Vector2 GetCoord(int col, int row)
	{
		return .(col * mCellWidth + GS!(6), row * mCellHeight + GS!(4));
	}

	public Vector2 GetCell(float x, float y)
	{
		return .((x - GS!(6)) / mCellWidth, (y - GS!(4)) / mCellHeight);
	}

	public override void Draw(Graphics g)
	{
		base.Draw(g);

		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
		String str = scope .(" ");

		uint32[16] colorTable = .(0xFF000000, );
		if (mScreenInfo != null)
		{
			for (int i < 16)
			{
				colorTable[i] = 0xFF000000 |
					((mScreenInfo.mInfo.mColorTable[i] >> 16) & 0x0000FF) |
					((mScreenInfo.mInfo.mColorTable[i]      ) & 0x00FF00) |
					((mScreenInfo.mInfo.mColorTable[i] << 16) & 0xFF0000);
			}
		}

		g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditBox), 0, 0, mWidth, mScrollableWidget.mHeight);
		using (g.PushColor(colorTable[0]))
			g.FillRect(GS!(2), GS!(2), mScrollableWidget.mVertScrollbar.mX - GS!(0), mScrollableWidget.mHeight - GS!(4));
		if (mView.mHasFocus)
		{
		    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
		        g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, mWidth, mHeight);
		}

		if (mScreenInfo != null)
		{
			g.SetFont(gApp.mTermFont);
			using (g.PushClip(0, 0, mScrollableWidget.mVertScrollbar.mX, mScrollableWidget.mHeight - GS!(2)))
			{
				int32 numVisibleCols = mScreenInfo.WindowWidth;
				int32 numVisibleRows = mScreenInfo.WindowHeight;

				for (int32 row < numVisibleRows)
				{
					for (int32 col < numVisibleCols)
					{
						int srcRow = row + mScreenInfo.mScrollTop;

						var coord = GetCoord(col, row);
						var cInfo = mScreenInfo.mCharInfo[row * mScreenInfo.WindowWidth + col];

						int32 attrs = cInfo.mAttributes;

						if (mScreenInfo.mSelectionInfo.mFlags != 0)
						{
							//TODO: Fix rendering, listen to flags.

							bool selected = false;
							if (srcRow == mScreenInfo.mSelectionInfo.mSelection.top)
							{
								selected = (col >= mScreenInfo.mSelectionInfo.mSelection.left);
								if (srcRow == mScreenInfo.mSelectionInfo.mSelection.bottom)
									selected &= (col <= mScreenInfo.mSelectionInfo.mSelection.right);
							}
							else if ((srcRow > mScreenInfo.mSelectionInfo.mSelection.top) && (srcRow < mScreenInfo.mSelectionInfo.mSelection.bottom))
							{
								selected = true;
							}
							else if (srcRow == mScreenInfo.mSelectionInfo.mSelection.bottom)
							{
								selected = (col <= mScreenInfo.mSelectionInfo.mSelection.right);
							}

							if (selected)
								attrs ^= 0xFF;
						}

						uint32 fgColor = colorTable[(attrs & 0xF)];
						uint32 bgColor = colorTable[(attrs >> 4)];


						using (g.PushColor(bgColor))
						{
							int32 fillX = (.)coord.mX;
							int32 fillY = (.)coord.mY;
							int32 fillWidth = mCellWidth;
							int32 fillHeight = mCellHeight;

							/*if (col == 0)
							{
								fillX -= GS!(4);
								fillWidth += GS!(4);
							}

							if (row == 0)
							{
								fillY -= GS!(2);
								fillHeight += GS!(2);
							}

							if (row == numVisibleRows - 1)
							{

							}

							g.FillRect(
								fillX, fillY,
								(col == numVisibleCols - 1) ? (mScrollableWidget.mVertScrollbar.mX - coord.mX + 1) : fillWidth,
								(row == numVisibleRows - 1) ? (mView.mHeight - coord.mY) : fillHeight
								);*/
							g.FillRect(fillX, fillY, fillWidth, fillHeight);
						}

						if (cInfo.mChar > .(32))
						{
							str[0] = (.)cInfo.mChar;
							using (g.PushColor(fgColor))
								gApp.mTermFont.Draw(g, str, coord.mX, coord.mY);
						}
					}
				}

				if ((mView.mHasFocus) && (mHasConsole) && (!mPaused))
				{
					float brightness = (float)Math.Cos(Math.Max(0.0f, mCursorBlinkTicks - 20) / 9.0f);
					brightness = Math.Clamp(brightness * 2.0f + 1.6f, 0, 1);
					if (mScrollableWidget.mVertPos.IsMoving)
					    brightness = 0; // When we animate a pgup or pgdn, it's weird seeing the cursor scrolling around

					if (brightness > 0)
					{
						using (g.PushColor(Color.Get(brightness)))
						{
							var cursorCoord = GetCoord(mScreenInfo.mInfo.mCursorX, mScreenInfo.mInfo.mCursorY - mScreenInfo.mScrollTop);
							if (mScreenInfo.mCursorInfo.mVisible != 0)
							{
								int32 cursorHeight = (int32)mScreenInfo.mCursorInfo.mSize * mCellHeight / 100;
								g.FillRect(cursorCoord.mX, cursorCoord.mY + mCellHeight - cursorHeight, mCellWidth, cursorHeight);
							}
						}
					}
				}
			}


			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			g.DrawString(scope $"Ln {mScreenInfo.mInfo.mCursorY + 1}", mWidth - GS!(120), mHeight - GS!(21));
			g.DrawString(scope $"Col {mScreenInfo.mInfo.mCursorX + 1}", mWidth - GS!(60), mHeight - GS!(21));
		}
	}

	public override void DrawAll(Graphics g)
	{
		if (!mHasConsole)
		{
			using (g.PushColor(0x80FFFFFF))
			{
				base.DrawAll(g);
			}
		}
		else if (mPaused)
		{
			using (g.PushColor(0xA0FFFFFF))
			{
				base.DrawAll(g);
			}
		}
		else
		{
			base.DrawAll(g);
		}
	}

	public void ResizeComponents()
	{
		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFOEX info = default;
		info.mSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);
#if BF_PLATFORM_WINDOWS
		GetConsoleScreenBufferInfoEx(outHandle, ref info);
#endif

		mCellWidth = (.)gApp.mTermFont.GetWidth('W');
		mCellHeight = (.)gApp.mTermFont.GetLineSpacing();

		mScrollableWidget.Resize(0, 0, mWidth, Math.Max(mHeight - GS!(22), 0));

		int32 cols = (.)((mWidth - GS!(2)) / mCellWidth);
		int32 rows = (.)((mScrollableWidget.mHeight - GS!(8)) / mCellHeight);

		mView.Resize(0, 0, mWidth, Math.Max(info.mHeight * mCellHeight, mHeight));
		mScrollableWidget.RehupSize();

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

	public override void Resize(float x, float y, float width, float height)
	{
		base.Resize(x, y, width, height);
		ResizeComponents();
	}

	public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
	{
		base.MouseDown(x, y, btn, btnCount);
		SetFocus();
	}

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

	public void SysKeyDown(KeyDownEvent keyEvent)
	{
		if (mPaused)
		{
			mPaused = false;
			return;
		}

		if (mView.mHasFocus)
		{
			mCursorBlinkTicks = 0;

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			if (keyEvent.mKeyCode != .Shift)
			{
				
			}

			INPUT_RECORD input = default;

			if (keyEvent.mKeyCode == .F1)
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
			}

			input.mEventType = 1 /*KEY_EVENT */;
			input.mEventData.mKeyEvent.mKeyDown = 1;
			input.mEventData.mKeyEvent.mRepeatCount = 1;
			input.mEventData.mKeyEvent.mVirtualKeyCode = (.)keyEvent.mKeyCode;
			//input.mEventData.mKeyEvent.mVirtualScanCode = 61;

			int keyMod = 0;
			if (keyEvent.mKeyFlags.HasFlag(.Ctrl))
			{
				keyMod |= 4;
			}
			else
			{
				if (keyEvent.mKeyFlags.HasFlag(.Shift))
					keyMod |= 1;
				if (keyEvent.mKeyFlags.HasFlag(.CapsLock))
					keyMod |= 2;
			}

			input.mEventData.mKeyEvent.mControlKeyState = GetControlKeyState(keyEvent.mKeyFlags);
			input.mEventData.mKeyEvent.mChar = (.)sKeyCharMap[(keyMod << 8) | (int)keyEvent.mKeyCode];
			
			var result = WriteConsoleInputW(inHandle, &input, 1, var numEventsWritten);
			int32 err = Windows.GetLastError();
		}	
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

	public void SysKeyUp(KeyCode keyCode)
	{
		if (mView.mHasFocus)
		{
			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);

			INPUT_RECORD input = default;
			input.mEventType = 1 /*KEY_EVENT */;
			input.mEventData.mKeyEvent.mVirtualKeyCode = (.)keyCode;
			WriteConsoleInputW(inHandle, &input, 1, var numEventsWritten);
		}
	}

	public override void MouseWheel(MouseEvent evt)
	{

	}

	public override void MouseWheel(float x, float y, float deltaX, float deltaY)
	{
		//base.MouseWheel(x, y, deltaX, deltaY);
	}
}