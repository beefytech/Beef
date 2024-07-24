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
using IDE.util;

namespace IDE.ui;

class ConsolePanel : Panel
{
	class View : Widget
	{
		public ConsolePanel mConsolePanel;
		public Vector2? mMousePos;
		public int32 mClickCount;

		public this(ConsolePanel ConsolePanel)
		{
			mConsolePanel = ConsolePanel;
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			mConsolePanel.mSelection = null;

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			if (mConsolePanel.MousePassThrough)
			{				
				var cell = mConsolePanel.GetCell(x, y);
				mConsolePanel.mConsoleProvider.MouseDown((.)cell.mCol, (.)cell.mRow, (.)mMouseFlags, btnCount, mWidgetWindow.GetKeyFlags(false));
			}
			else
			{
				if (btn == 0)
				{
					var cell = mConsolePanel.GetCell(x, y);
					mConsolePanel.mSelection = null;
					mConsolePanel.mClickPos = cell;
					mClickCount = btnCount;

					if (mClickCount > 1)
					{
						// Force selection
						MouseMove(x, y);
					}
				}
				else if (btn == 1)
				{
					var text = gApp.GetClipboardText(.. scope .());
					mConsolePanel.mConsoleProvider.SendInput(text);

					/*for (var c in text.DecodedChars)
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
					}*/
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

			mMousePos = .(x, y);
		}

		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);
			var cell = mConsolePanel.GetCell(x, y);
			mConsolePanel.mConsoleProvider.MouseMove((.)cell.mCol, (.)cell.mRow, (.)mMouseFlags, mWidgetWindow.GetKeyFlags(false));

			if (mMouseFlags.HasFlag(.Left))
			{
				if (mConsolePanel.mClickPos != null)
				{
					mConsolePanel.mSelection = (mConsolePanel.mClickPos.Value, cell);

					if (mClickCount > 1)
					{
						bool reversed = false;
						int startIdx = mConsolePanel.mSelection.Value.start.GetIndex(mConsolePanel.mConsoleProvider.Width, 0);
						int endIdx = mConsolePanel.mSelection.Value.end.GetIndex(mConsolePanel.mConsoleProvider.Width, 0);

						if (startIdx > endIdx)
						{
							reversed = true;
							Swap!(startIdx, endIdx);
						}

						if (mClickCount > 1)
						{
							while (true)
							{
								int checkStartIdx = startIdx - 1;
								var checkCell = mConsolePanel.mConsoleProvider.GetCell(checkStartIdx);
								if (!checkCell.mChar.IsLetterOrDigit)
									break;
								startIdx = checkStartIdx;
							}

							while (true)
							{
								int checkEndIdx = endIdx + 1;
								var checkCell = mConsolePanel.mConsoleProvider.GetCell(checkEndIdx);
								if (!checkCell.mChar.IsLetterOrDigit)
									break;
								endIdx = checkEndIdx;
							}
						}

						mConsolePanel.mSelection = (
							Position.FromIndex(reversed ? endIdx : startIdx, mConsolePanel.mConsoleProvider.Width),
							Position.FromIndex(reversed ? startIdx : endIdx, mConsolePanel.mConsoleProvider.Width));
					}
				}
			}

			mMousePos = .(x, y);
		}

		public override void MouseUp(float x, float y, int32 btn)
		{
			base.MouseUp(x, y, btn);
			var cell = mConsolePanel.GetCell(x, y);
			mConsolePanel.mConsoleProvider.MouseUp((.)cell.mCol, (.)cell.mRow, (.)mMouseFlags, mWidgetWindow.GetKeyFlags(false));

			if (btn == 0)
			{
				if (mConsolePanel.mSelection != null)
				{
					int32 numVisibleCols = (.)mConsolePanel.mConsoleProvider.Width;

					int selStart = mConsolePanel.mSelection.Value.start.GetIndex(numVisibleCols, 0);
					int selEnd = mConsolePanel.mSelection.Value.end.GetIndex(numVisibleCols, 0);

					if (selStart > selEnd)
						Swap!(selStart, selEnd);

					String str = scope .();
					for (int i in selStart ... selEnd)
					{
						var cellInfo = mConsolePanel.mConsoleProvider.GetCell(i);

						if ((numVisibleCols > 0) && (i % numVisibleCols == 0) && (i != selStart))
						{
							// Start of new line
							while ((str.Length > 0) && (str[str.Length - 1] == ' '))
								str.RemoveFromEnd(1);
							str.Append("\n");
						}

						str.Append(cellInfo.mChar);
					}

					gApp.SetClipboardText(str);
				}

				mConsolePanel.mClickPos = null;
			}

			mMousePos = null;
		}

		public override void KeyDown(KeyDownEvent keyEvent)
		{
			base.KeyDown(keyEvent);

			/*if (keyEvent.mKeyCode == .Insert)
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
				if (mConsolePanel.mConsoleProvider.Attached)
				{
					mConsolePanel.mConsoleProvider.Detach();
				}
				else
				{
					mConsolePanel.mConsoleProvider.Attach();

					ProcessStartInfo procInfo = scope ProcessStartInfo();
					procInfo.UseShellExecute = false;
					procInfo.SetFileName("Powershell.exe");

					String resultStr = scope String();
					var spawn = scope SpawnedProcess();
					spawn.Start(procInfo);

					mConsolePanel.ResizeComponents();
				}
			}*/
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
			if ((mConsolePanel.Paused) || (!mConsolePanel.mConsoleProvider.Attached))
			{
				base.MouseWheel(evt);
				return;
			}

			var cell = mConsolePanel.GetCell(evt.mX, evt.mY);
			mConsolePanel.mConsoleProvider.MouseWheel((.)cell.mCol, (.)cell.mRow, (.)evt.mWheelDeltaY);
		}
	}

	public struct Position
	{
		public int32 mCol;
		public int32 mRow;

		public this(int32 col, int32 row)
		{
			mCol = col;
			mRow = row;
		}

		public int32 GetIndex(int width, int32 rowOfs)
		{
			return (.)((mRow + rowOfs) * width + mCol);
		}

		public static Position FromIndex(int index, int width)
		{
			if (width == 0)
				return .((.)index, 0);
			return .((.)(index % width), (.)(index / width));
		}
	}

	public enum BeefConAttachState
	{
		case None;
		case Attached(int32 processId);
		case Connected(int32 processId);
	}

	public ConsoleProvider mConsoleProvider ~ delete _;
	public DarkCheckBox mMousePassthroughCB;
	public DarkCheckBox mPauseCB;
	public DarkScrollbar mScrollbar;
	public ScrollableWidget mScrollableWidget;
	public int32 mCellWidth;
	public int32 mCellHeight;
	public (Position start, Position end)? mSelection;
	public Position? mClickPos;
	public BeefConAttachState mBeefConAttachState;

	public bool Paused
	{
		get
		{
			return mPauseCB.Checked || (mSelection != null);
		}

		set
		{
			mPauseCB.Checked = value;
		}
	}

	public bool MousePassThrough
	{
		get
		{
			return mMousePassthroughCB.Checked;
		}

		set
		{
			mMousePassthroughCB.Checked = value;
		}
	}

	View mView;
	int mCursorBlinkTicks;
	

	public this()
	{
		mMousePassthroughCB = new DarkCheckBox();
		mMousePassthroughCB.Label = "Mouse Passthrough";
		AddWidget(mMousePassthroughCB);

		mPauseCB = new DarkCheckBox();
		mPauseCB.Label = "Pause";
		AddWidget(mPauseCB);

		/*mMouseComboBox = new DarkComboBox();
		mMouseComboBox.mFrameKind = .Frameless;
		mMouseComboBox.mPopulateMenuAction.Add(new (menu) =>
			{

			});
		AddWidget(mMouseComboBox);*/

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
				Paused = true;
			});
	}

	public virtual void Init()
	{
		mConsoleProvider = new WinNativeConsoleProvider();
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


	public override void Update()
	{
		base.Update();

		bool scrollQuantize = true;

		if ((mView.mMousePos != null) && (mView.mMouseFlags.HasFlag(.Left)) && (mSelection != null))
		{
			mWidgetWindow.RehupMouse(true);
			mView.SelfToParentTranslate(mView.mMousePos.Value.mX, mView.mMousePos.Value.mY, var parentX, var parentY);

			float scrollDelta = 0;
			if (parentY < 0)
			{
				scrollDelta = parentY;
			}
			else if (parentY > mView.mParent.mHeight)
			{
				scrollDelta = parentY - mView.mParent.mHeight;
			}

			if (scrollDelta != 0)
			{
				scrollQuantize = false;
				mScrollableWidget.VertScrollTo(mScrollableWidget.mVertPos.mDest + scrollDelta * 0.15f);
			}
		}

		float wantViewSize = Math.Max(mConsoleProvider.BufferHeight * mCellHeight, mHeight);
		if (mView.mHeight != wantViewSize)
			ResizeComponents();

		if (mScrollableWidget.mVertScrollbar.mThumb.mMouseDown)
		{
			Paused = true;
			scrollQuantize = false;
		}

		if (Paused)
		{
			if (mUpdateCnt % 30 == 0)
			{
				mPauseCB.Label = "Paused";
				for (int i < (mUpdateCnt / 30) % 4)
					mPauseCB.mLabel.Append(".");
				MarkDirty();
			}
		}
		else
			mPauseCB.Label = "Paused";

		if (mConsoleProvider.Update(Paused) case .Dirty)
			MarkDirty();

		/*if (mScreenInfo != null)
		{
			if ((mPaused) || (!mHasConsole))
			{
				mScreenInfo.mScrollTop = (.)(mScrollableWidget.mVertScrollbar.mContentPos / mCellHeight);
				
				int windowHeight = mScreenInfo.mInfo.mWindowRect.Height;
				mScreenInfo.mInfo.mWindowRect.top = (.)mScreenInfo.mScrollTop;
				mScreenInfo.mInfo.mWindowRect.bottom = (.)(mScreenInfo.mScrollTop + windowHeight);

				UpdateScreenInfo(mScreenInfo);
			}
		}*/

		if (Paused || !mConsoleProvider.Attached)
		{
			int scrollTop = (.)(mScrollableWidget.mVertScrollbar.mContentPos / mCellHeight);
			float wantScrollPos = scrollTop * mCellHeight;
			if ((wantScrollPos != mScrollableWidget.mVertScrollbar.mContentPos) && (scrollQuantize))
				mScrollableWidget.VertScrollTo(wantScrollPos, true);
			mConsoleProvider.ScrollTo(scrollTop);
		}
		else
		{
			mScrollableWidget.VertScrollTo(mConsoleProvider.ScrollTop * mCellHeight);
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

		

		//float height = mScreenInfo.mInfo.mHeight * mCellHeight;
		//mScrollableWidget.mScrollContent.Resize(0, 0, 0, height);
		//mScrollableWidget.RehupSize();

		mCursorBlinkTicks++;
		if (mView.mHasFocus)
			MarkDirty();
	}


	public Vector2 GetCoord(int col, int row)
	{
		return .(col * mCellWidth + GS!(6), row * mCellHeight + GS!(4));
	}

	public Position GetCell(float x, float y)
	{
		return .((.)((x - GS!(6)) / mCellWidth), (.)((y - GS!(4)) / mCellHeight));
	}

	public override void Draw(Graphics g)
	{
		base.Draw(g);

		var outHandle = Console.[Friend]GetStdHandle(Console.STD_OUTPUT_HANDLE);
		String str = scope .(" ");

		g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditBox), 0, 0, mWidth, mScrollableWidget.mHeight);
		using (g.PushColor(mConsoleProvider.GetColor(0)))
			g.FillRect(GS!(2), GS!(2), mScrollableWidget.mVertScrollbar.mX - GS!(0), mScrollableWidget.mHeight - GS!(4));
		if (mView.mHasFocus)
		{
		    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
		        g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, mWidth, mHeight);
		}

		int32 topOfs = (.)mConsoleProvider.ScrollTop;
		var cursorPos = mConsoleProvider.CursorPos;

		g.SetFont(gApp.mTermFont);
		using (g.PushClip(0, 0, mScrollableWidget.mVertScrollbar.mX, mScrollableWidget.mHeight - GS!(2)))
		{
			int32 numVisibleCols = (.)mConsoleProvider.Width;
			int32 numVisibleRows = (.)mConsoleProvider.Height;

			int32 selStart = -1;
			int32 selEnd = -1;

			if (mSelection != null)
			{
				selStart = mSelection.Value.start.GetIndex(numVisibleCols, -topOfs);
				selEnd = mSelection.Value.end.GetIndex(numVisibleCols, -topOfs);

				if (selStart > selEnd)
					Swap!(selStart, selEnd);

				/*if (mSelection.Value.start.GetIndex(numVisibleCols, -topOfs) < mSelection.Value.end.GetIndex(numVisibleCols, -topOfs))
				{
					
				}
				else
				{
					selStart = mSelection.Value.end.GetIndex(numVisibleCols, -topOfs);
					selEnd = mSelection.Value.start.GetIndex(numVisibleCols, -topOfs);
				}*/
			}

			for (int32 row < numVisibleRows)
			{
				for (int32 col < numVisibleCols)
				{
					int32 cellIdx = row * numVisibleCols + col;

					int srcRow = row + mConsoleProvider.ScrollTop;

					var coord = GetCoord(col, row);
					var cInfo = mConsoleProvider.GetCell(col, row);

					int32 attrs = cInfo.mAttributes;

					if ((cellIdx >= selStart) && (cellIdx <= selEnd))
						attrs ^= 0xFF;

					/*if (mScreenInfo.mSelectionInfo.mFlags != 0)
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
					}*/

					uint32 fgColor = mConsoleProvider.GetColor(attrs & 0xF);
					uint32 bgColor = mConsoleProvider.GetColor(attrs >> 4);

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

			if ((mView.mHasFocus) && (mConsoleProvider.Connected) && (!Paused))
			{
				float brightness = (float)Math.Cos(Math.Max(0.0f, mCursorBlinkTicks - 20) / 9.0f);
				brightness = Math.Clamp(brightness * 2.0f + 1.6f, 0, 1);
				if (mScrollableWidget.mVertPos.IsMoving)
				    brightness = 0; // When we animate a pgup or pgdn, it's weird seeing the cursor scrolling around

				if (brightness > 0)
				{
					using (g.PushColor(Color.Get(brightness)))
					{
						var cursorCoord = GetCoord(cursorPos.col, cursorPos.row - mConsoleProvider.ScrollTop);
						if (mConsoleProvider.CursorVisible)
						{
							int32 cursorHeight = (int32)(mConsoleProvider.CursorHeight * mCellHeight);
							g.FillRect(cursorCoord.mX, cursorCoord.mY + mCellHeight - cursorHeight, mCellWidth, cursorHeight);
						}
					}
				}
			}
		}

		g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
		g.DrawString(scope $"Ln {cursorPos.row + 1}", mWidth - GS!(120), mHeight - GS!(21));
		g.DrawString(scope $"Col {cursorPos.col + 1}", mWidth - GS!(60), mHeight - GS!(21));
	}

	public override void DrawAll(Graphics g)
	{
		if (!mConsoleProvider.Attached)
		{
			//using (g.PushColor(0x80FFFFFF))
			using (g.PushColor(0xFFA0A0A0))
			{
				base.DrawAll(g);
			}
		}
		else if (mPauseCB.Checked)
		{
			using (g.PushColor(0xFFD0D0D0))
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
		mCellWidth = (.)gApp.mTermFont.GetWidth('W');
		mCellHeight = (.)gApp.mTermFont.GetLineSpacing();

		mScrollableWidget.Resize(0, 0, mWidth, Math.Max(mHeight - GS!(22), 0));

		int32 cols = (.)((mWidth - GS!(2)) / mCellWidth);
		int32 rows = (.)((mScrollableWidget.mHeight - GS!(8)) / mCellHeight);

		mView.Resize(0, 0, mWidth, Math.Max(mConsoleProvider.BufferHeight * mCellHeight, mHeight));
		mScrollableWidget.RehupSize();

		mConsoleProvider.Resize(cols, rows, Paused || !mConsoleProvider.Attached);

		//mMouseComboBox.Resize(GS!(8), mHeight - GS!(22), GS!(100), GS!(24));
		mMousePassthroughCB.Resize(GS!(8), mHeight - GS!(20), GS!(140), GS!(24));
		mPauseCB.Resize(GS!(170), mHeight - GS!(20), GS!(140), GS!(24));
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

	public void SysKeyDown(KeyDownEvent keyEvent)
	{
		if (Paused)
		{
			mSelection = null;
			Paused = false;
			return;
		}

		if (mView.mHasFocus)
		{
			mCursorBlinkTicks = 0;

			mConsoleProvider.KeyDown(keyEvent.mKeyCode, keyEvent.mKeyFlags);

			var inHandle = Console.[Friend]GetStdHandle(Console.STD_INPUT_HANDLE);
			if (keyEvent.mKeyCode != .Shift)
			{
				
			}
		}	
	}
	
	public void SysKeyUp(KeyCode keyCode)
	{
		if (mView.mHasFocus)
		{
			mConsoleProvider.KeyUp(keyCode);
		}
	}

	public override void MouseWheel(MouseEvent evt)
	{

	}

	public override void MouseWheel(float x, float y, float deltaX, float deltaY)
	{
		//base.MouseWheel(x, y, deltaX, deltaY);
	}

	public void Attach()
	{
		if (mConsoleProvider.Attached)
			return;
		mConsoleProvider.Attach();
		ResizeComponents();
	}

	public void Detach()
	{
		if (mBeefConAttachState case .Attached)
		{
#if BF_PLATFORM_WINDOWS
			WinNativeConsoleProvider.FreeConsole();
#endif
			mBeefConAttachState = .None;
		}

		if (!mConsoleProvider.Attached)
			return;
		mConsoleProvider.Detach();
	}

	public override void FocusForKeyboard()
	{
		mView.SetFocus();
	}
}