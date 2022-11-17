#include <dsound.h>
#include "WinBFApp.h"
#include "DXRenderDevice.h"
#include <signal.h>
#include "../../util/BeefPerf.h"
#include "DSoundManager.h"
#include "DInputManager.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "util/AllocDebug.h"

USING_NS_BF;

int Beefy::WinBFMenu::mMenuCount = 0;

int PrintStuff()
{
	OutputDebugStringA("Hey!\n");
	return 123;
}

WinBFMenu::WinBFMenu()
{
	mIsPlaceholder = false;
	mMenu = NULL;
	mParent = NULL;
	mMenuId = 0;
}

///

static HICON gMainIcon = NULL;

static BOOL CALLBACK BFEnumResNameProc(
  HMODULE hModule,
  LPCWSTR lpszType,
  LPWSTR lpszName,
  LONG_PTR lParam
)
{
	gMainIcon = ::LoadIconW(hModule, lpszName);
	return FALSE;
}

struct AdjustedMonRect
{
	int mMonCount;
	int mX;
	int mY;
	int mWidth;
	int mHeight;
};

static BOOL ClipToMonitor(HMONITOR mon, HDC hdc, LPRECT monRect, LPARAM userArg)
{
	AdjustedMonRect* outRect = (AdjustedMonRect*)userArg;

	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (::GetMonitorInfo(mon, &monitorInfo) == 0)
		return TRUE;

	outRect->mMonCount++;

	if (outRect->mX < monitorInfo.rcWork.left)
		outRect->mX = monitorInfo.rcWork.left;
 	else if (outRect->mX + outRect->mWidth >= monitorInfo.rcWork.right)
		outRect->mX = BF_MAX((int)monitorInfo.rcWork.left, monitorInfo.rcWork.right - outRect->mWidth);

 	if (outRect->mY < monitorInfo.rcWork.top)
		outRect->mY = monitorInfo.rcWork.top;
 	else if (outRect->mY + outRect->mHeight >= monitorInfo.rcWork.bottom)
		outRect->mY = BF_MAX((int)monitorInfo.rcWork.top, monitorInfo.rcWork.bottom - outRect->mHeight);

	return TRUE;
}

static BOOL KeyboardLayoutHasAltGr(HKL layout)
{
	BOOL hasAltGr = FALSE;
	int scancode;
	for (WORD i = 32; i < 256; ++i)
	{
	    scancode = VkKeyScanEx((TCHAR)i, layout);
	    if ((scancode != -1) && ((scancode & 0x600) == 0x600))
	    {
	        hasAltGr = TRUE;
	        break;
	    }
	}
	return hasAltGr;
}

WinBFWindow::WinBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags)
{
	//OutputDebugStrF("Wnd %p Create\n", this);

	HINSTANCE hInstance = GetModuleHandle(NULL);

	mMinWidth = 128;
	mMinHeight = 128;

	WNDCLASSW wc;
	wc.style = 0;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = NULL;
	//wc.hbrBackground = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
	wc.hCursor = NULL;
	//wc.hIcon = (HICON) ::LoadImageA(hInstance, "MainIcon", IMAGE_ICON, 0, 0, 0);
	//wc.hIcon = (HICON) ::LoadImageA(hInstance, MAKEINTRESOURCEA(32512), IMAGE_ICON, 0, 0, 0);
	if (gMainIcon != NULL)
	{
		wc.hIcon = gMainIcon;
	}
	else
	{
		wc.hIcon = (HICON) ::LoadIconA(hInstance, "MainIcon");
		if (wc.hIcon == NULL)
		{
			EnumResourceNamesW(hInstance, (LPCWSTR)RT_GROUP_ICON, BFEnumResNameProc, 0);
			wc.hIcon = gMainIcon;
		}
	}

	wc.hInstance = hInstance;
	wc.lpfnWndProc = WindowProcStub;
	wc.lpszClassName = L"BFWindow";
	wc.lpszMenuName = NULL;
	RegisterClassW(&wc);

	int requestedX = x;
	int requestedY = y;

	int aWindowFlags = 0;
	bool hasDestAlpha = (windowFlags & BFWINDOW_DEST_ALPHA) != 0;

	if (windowFlags & BFWINDOW_MENU)
	{
		WinBFMenu* aMenu = new WinBFMenu();
		aMenu->mMenu = ::CreateMenu();
		mHMenuMap[aMenu->mMenu] = aMenu;
		mMenu = aMenu;
	}

	int windowFlagsEx = /*WS_EX_COMPOSITED |*/ WS_EX_LAYERED;
	if (windowFlags & BFWINDOW_TOOLWINDOW)
		windowFlagsEx |= WS_EX_TOOLWINDOW;
	if (windowFlags & BFWINDOW_BORDER)
		aWindowFlags |= WS_BORDER;
	if (windowFlags & BFWINDOW_THICKFRAME)
		aWindowFlags |= WS_THICKFRAME;
	if ((windowFlags & BFWINDOW_RESIZABLE) && (!hasDestAlpha))
		aWindowFlags |= WS_SIZEBOX;
	if (windowFlags & BFWINDOW_SYSMENU)
		aWindowFlags |= WS_SYSMENU;
	if (windowFlags & BFWINDOW_CAPTION)
		aWindowFlags |= WS_CAPTION;
	else
		aWindowFlags |= WS_POPUP;
	if (windowFlags & BFWINDOW_MINIMIZE)
		aWindowFlags |= WS_MINIMIZEBOX;
	if (windowFlags & BFWINDOW_MAXIMIZE)
		aWindowFlags |= WS_MAXIMIZEBOX;
	if ((windowFlags & BFWINDOW_TOPMOST) && (parent == NULL))
		windowFlagsEx |= WS_EX_TOPMOST;
	if ((windowFlags & BFWINDOW_ACCEPTFILES))
		windowFlagsEx |= WS_EX_ACCEPTFILES;

	if (windowFlags & BFWINDOW_CLIENT_SIZED)
	{
		RECT rect = {0, 0, width, height};
		AdjustWindowRectEx(&rect, aWindowFlags, mMenu != NULL, windowFlagsEx);
		x += rect.left;
		y += rect.top;
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;
	}

	if (windowFlags & BFWINDOW_POPUP_POSITION)
	{
		AdjustedMonRect adjustRect = { 0, x, y, width, height };
		RECT wantRect = { x, y, x + width, y + height };

		EnumDisplayMonitors(NULL, &wantRect, ClipToMonitor, (LPARAM)&adjustRect);
		if (adjustRect.mMonCount == 0)
			EnumDisplayMonitors(NULL, NULL, ClipToMonitor, (LPARAM)&adjustRect);
		x = adjustRect.mX;
		y = adjustRect.mY;
		width = adjustRect.mWidth;
		height = adjustRect.mHeight;
	}

	mFlags = windowFlags;
	mMouseVisible = true;

	mParent = parent;
	HWND parentHWnd = NULL;
	if (parent != NULL)
		parentHWnd = ((WinBFWindow*)  parent)->mHWnd;

	if (mMenu != NULL)
	{
		WinBFMenu* placeholderMenu = (WinBFMenu*) AddMenuItem(mMenu, 0, ": Placeholder Menu Item :", NULL, NULL, false, -1, false);
		placeholderMenu->mIsPlaceholder = true;
	}

	mHWnd = CreateWindowExW(windowFlagsEx, L"BFWindow", UTF8Decode(title).c_str(),
		aWindowFlags,
		x, y,
		width,
		height,
		parentHWnd,
		(mMenu != NULL) ? ((WinBFMenu*) mMenu)->mMenu : NULL,
		hInstance,
		0);

	if ((windowFlags & BFWINDOW_ALPHA_MASK) == 0)
		SetLayeredWindowAttributes(mHWnd, 0, 255, 0);

	HWND relativeWindow = NULL;
	if ((windowFlags & BFWINDOW_TOPMOST) && (parent == NULL))
		relativeWindow = HWND_TOP;
	int showFlags = SWP_SHOWWINDOW;
// 	if (windowFlags & BFWINDOW_SHOWMINIMIZED)
// 		showFlags = SWP_
	mHasFocus = true;
	mSoftHasFocus = true;
	if (windowFlags & BFWINDOW_FAKEFOCUS)
	{
		showFlags |= SWP_NOACTIVATE;
		if (windowFlags & BFWINDOW_NO_ACTIVATE)
		{
			mHasFocus = false;
			mSoftHasFocus = false;
		}
	}
	else if (windowFlags & BFWINDOW_NO_ACTIVATE)
	{
		showFlags |= SWP_NOACTIVATE;
		mHasFocus = false;
		mSoftHasFocus = false;
	}

	if ((windowFlags & BFWINDOW_NOSHOW))
		showFlags = SWP_HIDEWINDOW;

	if (windowFlags & (BFWINDOW_SHOWMINIMIZED | BFWINDOW_SHOWMAXIMIZED))
	{
		WINDOWPLACEMENT wndPlacement = { sizeof(WINDOWPLACEMENT), 0 };
		::GetWindowPlacement(mHWnd, &wndPlacement);

		if (windowFlags & BFWINDOW_SHOWMINIMIZED)
			wndPlacement.showCmd = SW_SHOWMINIMIZED;
		else if (windowFlags & BFWINDOW_SHOWMAXIMIZED)
			wndPlacement.showCmd = SW_SHOWMAXIMIZED;
		else
			wndPlacement.showCmd = SW_SHOWNORMAL;

		wndPlacement.rcNormalPosition.left = x;
		wndPlacement.rcNormalPosition.top = y;
		wndPlacement.rcNormalPosition.right = x + width;
		wndPlacement.rcNormalPosition.bottom = y + height;
		::SetWindowPlacement(mHWnd, &wndPlacement);
	}
	else
	{
		SetWindowPos(mHWnd, relativeWindow, x, y, width, height, showFlags);
	}


	SetTimer(mHWnd, 0, 10, NULL);

	mIsMouseInside = false;
	mRenderWindow = new DXRenderWindow((DXRenderDevice*) gBFApp->mRenderDevice, this, (windowFlags & BFWINDOW_FULLSCREEN) == 0);
	gBFApp->mRenderDevice->AddRenderWindow(mRenderWindow);

	SetWindowLongPtr(mHWnd, GWLP_USERDATA, (LONG_PTR)this);

	mIsMenuKeyHandled = false;
	mAlphaMaskBitmap = NULL;
	mAlphaMaskDC = NULL;
	mAlphaMaskPixels = NULL;
	mAlphaMaskWidth = 0;
	mAlphaMaskHeight = 0;
	mNeedsStateReset = false;
	mAwaitKeyReleases = false;
	mAwaitKeyReleasesEventTick = 0;
	mAwaitKeyReleasesCheckIdx = 0;
	mFocusLostTick = ::GetTickCount();

	if (windowFlags & BFWINDOW_DEST_ALPHA)
	{
		MARGINS dWMMargins = {-1, -1, -1, -1};
		DwmExtendFrameIntoClientArea(mHWnd, &dWMMargins);
	}

	if (windowFlags & BFWINDOW_MODAL)
	{
		EnableWindow(parentHWnd, FALSE);
	}

	if (parent != NULL)
	{
		auto winParent = (WinBFWindow*)parent;
		BF_ASSERT(winParent->mHWnd);
		parent->mChildren.push_back(this);
	}

	HKL layout = GetKeyboardLayout(0);
	mKeyLayoutHasAltGr = (KeyboardLayoutHasAltGr(layout) == TRUE);
}

WinBFWindow::~WinBFWindow()
{
	//OutputDebugStrF("Wnd %p Destroyed\n", this);

	if (mHWnd != NULL)
		Destroy();
}

void* WinBFWindow::GetUnderlying()
{
	return mHWnd;
}

void WinBFWindow::Destroy()
{
	if (mAlphaMaskDC != NULL)
		DeleteDC(mAlphaMaskDC);
	mAlphaMaskDC = NULL;

	if (mAlphaMaskBitmap != NULL)
		DeleteObject(mAlphaMaskBitmap);
	mAlphaMaskBitmap = NULL;

	for (auto& menu : mMenuIDMap)
	{
		delete menu.mValue;
	}
	mMenuIDMap.Clear();

	::DestroyWindow(mHWnd);
	BF_ASSERT(mHWnd == NULL);
}

bool WinBFWindow::TryClose()
{
	SendMessage(mHWnd, WM_CLOSE, 0, 0);
	return mHWnd == NULL;
}

void WinBFWindow::SetTitle(const char* title)
{
	SetWindowTextA(mHWnd, title);
}

static int ToWShow(BFWindow::ShowKind showKind)
{
	switch (showKind)
	{
	case BFWindow::ShowKind_Hide: return SW_HIDE;
	case BFWindow::ShowKind_Normal: return SW_NORMAL;
	case BFWindow::ShowKind_Minimized: return SW_MINIMIZE;
	case BFWindow::ShowKind_Maximized: return SW_MAXIMIZE;
	case BFWindow::ShowKind_Show: return SW_SHOW;
	case BFWindow::ShowKind_ShowNormal: return SW_SHOWNORMAL;
	case BFWindow::ShowKind_ShowMinimized: return SW_SHOWMINIMIZED;
	case BFWindow::ShowKind_ShowMaximized: return SW_SHOWMAXIMIZED;
	}
	return SW_SHOW;
}

void WinBFWindow::Show(ShowKind showKind)
{
	::ShowWindow(mHWnd, ToWShow(showKind));
}

void WinBFWindow::LostFocus(BFWindow* newFocus)
{
	///OutputDebugStrF("Lost focus\n");
	mFocusLostTick = ::GetTickCount();
	WinBFWindow* bfNewFocus = (WinBFWindow*)newFocus;
	mSoftHasFocus = false;
	for (int i = 0; i < KEYCODE_MAX; i++)
	{
		// Only transfer mode keys
		if (mIsKeyDown[i])
		{
			mIsKeyDown[i] = false;
			mKeyUpFunc(this, i);

			if ((newFocus != NULL) &&
				((i == VK_SHIFT) || (i == VK_CONTROL) || (i == VK_MENU)))
			{
				newFocus->mIsKeyDown[i] = true;
				newFocus->mKeyDownFunc(newFocus, i, 0);
			}
		}
	}
}

void WinBFWindow::GotFocus()
{
	DWORD tickNow = ::GetTickCount();

	//OutputDebugStrF("GotFocus since lost %d\n", tickNow - mFocusLostTick);

	if (tickNow - mFocusLostTick >= 1000)
	{
		mAwaitKeyReleases = true;
		mAwaitKeyReleasesCheckIdx = 0;
		mAwaitKeyReleasesEventTick = ::GetTickCount();
	}
}

void WinBFWindow::SetForeground()
{
	bool hadFocus = mHasFocus;
	DWORD prevFocusLostTick = mFocusLostTick;

	if (mFlags & BFWINDOW_FAKEFOCUS)
	{
		mHasFocus = true;
		mSoftHasFocus = true;
		return;
	}

	::SetFocus(mHWnd);
	::SetForegroundWindow(mHWnd);


	//OutputDebugStrF("SetForeground %p %d %d %d\n", mHWnd, hadFocus, ::GetTickCount() - prevFocusLostTick, mAwaitKeyReleases);
}

static POINT gLastScreenMouseCoords = { -1, -1 };

void WinBFWindow::RehupMouseOver(bool isMouseOver)
{
	if ((!mIsMouseInside) && (isMouseOver))
	{
		TRACKMOUSEEVENT tme;
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = mHWnd;
		TrackMouseEvent(&tme);
		mIsMouseInside = true;
	}

	if ((mIsMouseInside) && (!isMouseOver))
	{
		mIsMouseInside = false;
		mMouseLeaveFunc(this);
	}
}

bool WinBFWindow::CheckKeyReleases(bool isKeyDown)
{
	if (!mHasFocus)
		GotFocus();

	if (!mAwaitKeyReleases)
		return true;

	// Time expired with no key presses
	if ((mAwaitKeyReleasesEventTick != 0) && (mAwaitKeyReleasesCheckIdx == 0) && (::GetTickCount() - mAwaitKeyReleasesEventTick > 150))
	{
		//OutputDebugStrF("CheckKeyReleases no initial key press\n");
		mAwaitKeyReleases = false;
		return true;
	}
	mAwaitKeyReleasesCheckIdx++;

	bool hasKeyDown = false;
	uint8 keysDown[256] = { 0 };
	::GetKeyboardState((PBYTE)&keysDown);
	for (int i = 1; i < 256; i++)
		if (keysDown[i] & 0x80)
			hasKeyDown = true;

	if ((hasKeyDown) && (::GetTickCount() - mAwaitKeyReleasesEventTick >= 600))
	{
// 		String dbgStr = "CheckKeyReleases timeout. Keys down:";
// 		for (int i = 1; i < 256; i++)
// 			if (keysDown[i] & 0x80)
// 				dbgStr += StrFormat(" %2X", i);
// 		dbgStr += "\n";
// 		OutputDebugStr(dbgStr);
		hasKeyDown = false;
	}

	if (!hasKeyDown)
	{
		mAwaitKeyReleases = false;
		mAwaitKeyReleasesCheckIdx = 0;
		mAwaitKeyReleasesEventTick = 0;
	}
	return !mAwaitKeyReleases;
}

LRESULT WinBFWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WinBFApp* app = (WinBFApp*) gBFApp;

	if (app == NULL)
	{
		PrintStuff();
	}

	switch (uMsg)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_CHAR:
		for (auto child : mChildren)
		{
			auto childWindow = (WinBFWindow*)child;
			if ((childWindow->mSoftHasFocus) && (childWindow->mFlags & BFWINDOW_FAKEFOCUS))
			{
				return childWindow->WindowProc(hWnd, uMsg, wParam, lParam);
			}
		}
		break;
	}

	switch (uMsg)
	{
	case WM_CLOSE:
		{
			//OutputDebugStrF("WM_CLOSE %08X NewFocus:%08X\n", hWnd, GetFocus());

			if (mCloseQueryFunc(this) != 0)
				gBFApp->RemoveWindow(this);
			HWND newFocus = GetFocus();
			for (auto window : gBFApp->mWindowList)
			{
				WinBFWindow* winWindow = (WinBFWindow*)window;
				if (winWindow->mHWnd == newFocus)
				{
					while (true)
					{
						WinBFWindow* altFocusWindow = NULL;
						for (auto checkChild : winWindow->mChildren)
						{
							auto checkWinChild = (WinBFWindow*)checkChild;
							if (checkWinChild->mFlags & BFWINDOW_FAKEFOCUS)
								altFocusWindow = checkWinChild;
						}
						if (altFocusWindow == NULL)
							break;
						winWindow = altFocusWindow;
					}

					winWindow->mHasFocus = true;
					winWindow->mSoftHasFocus = true;
					winWindow->mGotFocusFunc(winWindow);
				}
			}

			return 0;
		}
		break;
	case WM_DESTROY:
		/*if (mFlags & BFWINDOW_QUIT_ON_CLOSE)
		{
			gBFApp->mRunning = false;
		}*/
		SetWindowLongPtr(mHWnd, GWLP_USERDATA, (LONG_PTR)0);
		mHWnd = NULL;
		if (!mChildren.IsEmpty())
		{
			NOP;
		}
		break;
	}

	LRESULT result = 0;
	bool doResult = false;

	if (!app->mInMsgProc)
	{
		if (mNeedsStateReset)
		{
			for (int i = 0; i < KEYCODE_MAX; i++)
			{
				if (mIsKeyDown[i])
				{
					mIsKeyDown[i] = false;
					mKeyUpFunc(this, i);
				}
			}

			POINT mousePoint;
			::GetCursorPos(&mousePoint);
			::ScreenToClient(hWnd, &mousePoint);

			for (int i = 0; i < MOUSEBUTTON_MAX; i++)
			{
				if (mIsMouseDown[i])
				{
					mMouseUpFunc(this, mousePoint.x, mousePoint.y, i);
					mIsMouseDown[i] = false;

					//OutputDebugStrF("Wnd %p mNeedsStateReset MouseUp %d\n", this, i);
				}
			}

			//OutputDebugStrF("Rehup ReleaseCapture()\n");
			ReleaseCapture();

			mNeedsStateReset = false;
			mIsMouseInside = false;
		}

		WinBFWindow* menuTarget = this;
		if (mFlags & BFWINDOW_USE_PARENT_MENU)
			menuTarget = ((WinBFWindow*)mParent);
		auto* menuIDMap = &menuTarget->mMenuIDMap;
		auto* hMenuMap = &menuTarget->mHMenuMap;

		app->mInMsgProc = true;

		switch (uMsg)
		{
		case WM_DISPLAYCHANGE:
			((DXRenderWindow*)mRenderWindow)->mRefreshRate = 0;
			break;
		case WM_SIZE:
			mRenderWindow->Resized();
			if (mMovedFunc != NULL)
				mMovedFunc(this);
			break;
		case WM_PAINT:
			break;
		case WM_NCHITTEST:
			{
				//OutputDebugStrF("WM_NCHITTEST %X\n", mHWnd);

				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				result = mHitTestFunc(this, x, y);
				doResult = (result != -3);
			}
			break;
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_MOUSEMOVE:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				bool releaseCapture = false;

				POINT point = {x, y};
				if ((uMsg != WM_MOUSEWHEEL) && (uMsg != WM_MOUSEHWHEEL))
					::ClientToScreen(hWnd, &point);

				if ((uMsg == WM_MOUSEMOVE) && (point.x == gLastScreenMouseCoords.x) && (point.y == gLastScreenMouseCoords.y))
				{
					// Don't process a WM_MOUSEMOVE if it's at the same point at the last mouse event
					//  This keeps us from getting a WM_MOUSEMOVE when we popup a window under the current cursor location.
					//  This is important for keyboard cursor control - so the first down arrow keypress selects the first item in the list
					//  irregardless of the mouse cursor position.
					break;
				}
				gLastScreenMouseCoords.x = point.x;
				gLastScreenMouseCoords.y = point.y;

				HWND windowAtPoint = ::WindowFromPoint(point);

				bool isMouseOver = windowAtPoint == hWnd;
				RehupMouseOver(isMouseOver);
				//OutputDebugStrF("HWnd: %X  Focus Window: %X  Capture: %X\n", hWnd, windowAtPoint, ::GetCapture());

				bool checkNonTransparentMousePosition = mNonExclusiveMouseCapture;

				auto _BtnDown = [&](int btn)
				{
					BF_ASSERT(btn < MOUSEBUTTON_MAX);
					if (btn >= MOUSEBUTTON_MAX)
						return;

					//OutputDebugStrF("Wnd %p BtnDown %d\n", this, btn);

					DWORD tickNow = BFTickCount();

					if (::SetCapture(hWnd) != hWnd)
					{
						// Not captured, no buttons were down
						for (int i = 0; i < MOUSEBUTTON_MAX; i++)
						{
							if (mIsMouseDown[i])
							{
								mMouseUpFunc(this, x, y, i);
								mIsMouseDown[i] = false;
								//OutputDebugStrF("Wnd %p BtnDown MouseUp %d\n", this, i);
							}
						}
					}
					mIsMouseDown[btn] = true;
					BFCoord mouseCoords = { x, y };
					if ((mouseCoords.mX != mMouseDownCoords[btn].mX) || (mouseCoords.mY != mMouseDownCoords[btn].mY) ||
						(tickNow - mMouseDownTicks[btn] > ::GetDoubleClickTime()))
						mMouseClickCount[btn] = 0;
					mMouseDownCoords[btn] = mouseCoords;
					mMouseClickCount[btn]++;
					mMouseDownTicks[btn] = tickNow;
					mMouseDownFunc(this, x, y, btn, mMouseClickCount[btn]);
				};

				auto _BtnUp = [&](int btn)
				{
					BF_ASSERT(btn < MOUSEBUTTON_MAX);
					if (btn >= MOUSEBUTTON_MAX)
						return;

					//OutputDebugStrF("Wnd %p BtnUp %d\n", this, btn);

					releaseCapture = true;
					mIsMouseDown[btn] = false;
					mMouseUpFunc(this, x, y, btn);
				};

				switch (uMsg)
				{
				case WM_LBUTTONDOWN:
					//OutputDebugStrF("WM_LBUTTONDOWN Capture HWnd: %X\n", hWnd);
					_BtnDown(0);
					break;
				case WM_RBUTTONDOWN:
					//OutputDebugStrF("WM_RBUTTONDOWN Capture HWnd: %X\n", hWnd);
					_BtnDown(1);
					break;
				case WM_MBUTTONDOWN:
					_BtnDown(2);
					break;
				case WM_XBUTTONDOWN:
					_BtnDown((int)(wParam >> 16) + 2);
					break;
				case WM_LBUTTONUP:
					_BtnUp(0);
					break;
				case WM_RBUTTONUP:
					_BtnUp(1);
					break;
				case WM_MBUTTONUP:
					_BtnUp(2);
					break;
				case WM_XBUTTONUP:
					_BtnUp((int)(wParam >> 16) + 2);
					break;
				case WM_MOUSEWHEEL:
				case WM_MOUSEHWHEEL:
					{
						WinBFWindow* cursorWindow = this;

						if ((gBFApp->mWindowList.size() > 1) && (GetCapture() == NULL))
						{
							// See if our mouse is down and has entered into another window's space
							POINT point = { x, y };
							HWND windowAtPoint = ::WindowFromPoint(point);

							BFWindowList::iterator itr = gBFApp->mWindowList.begin();
							while (itr != gBFApp->mWindowList.end())
							{
								WinBFWindow* aWindow = (WinBFWindow*) *itr;
								LONG targetStyle = ::GetWindowLong(aWindow->mHWnd, GWL_EXSTYLE);
								if ((::IsWindowEnabled(aWindow->mHWnd)) && ((targetStyle & WS_EX_TRANSPARENT) == 0))
								{
									if (aWindow->mHWnd == windowAtPoint)
									{
										aWindow->mIsMouseInside = true;
										cursorWindow = aWindow;
									}
								}
								++itr;
							}
						}

						UINT ucNumLines = 0;
						SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &ucNumLines, 0);
						if (ucNumLines == 0)
							ucNumLines = 3; // Default

						if ((cursorWindow != this) && (mIsMouseInside))
						{
							mMouseLeaveFunc(this);
							mIsMouseInside = false;
						}

						POINT pt = {x, y};
						ScreenToClient(cursorWindow->mHWnd, &pt);

						if (uMsg == WM_MOUSEWHEEL)
						{
							float delta = ((int16)HIWORD(wParam)) / 120.0f * (float)ucNumLines;
							mMouseWheelFunc(cursorWindow, pt.x, pt.y, 0, delta);
						}
						else
						{
							float delta = ((int16)HIWORD(wParam)) / 120.0f;
							mMouseWheelFunc(cursorWindow, pt.x, pt.y, delta, 0);
						}
					}
					break;
				case WM_MOUSEMOVE:
					{
						//OutputDebugStrF("WM_MOUSEMOVE %d\n", hWnd);
						mMouseMoveFunc(this, x, y);

						// If we are dragging a transparent window then check for mouse positions under cursor
						HWND captureWindow = GetCapture();
						if (captureWindow != NULL)
						{
							LONG captureStyle = ::GetWindowLong(captureWindow, GWL_EXSTYLE);
							if ((captureStyle & WS_EX_TRANSPARENT) != 0)
								checkNonTransparentMousePosition = true;
						}
					}
					break;
				}

				if ((checkNonTransparentMousePosition) && (gBFApp->mWindowList.size() > 1))
				{
					// See if our mouse is down and has entered into another window's space
					POINT point = { x, y };
					::ClientToScreen(hWnd, &point);

					HWND windowAtPoint = ::WindowFromPoint(point);

					BFWindowList::iterator itr = gBFApp->mWindowList.begin();
					while (itr != gBFApp->mWindowList.end())
					{
						WinBFWindow* aWindow = (WinBFWindow*) *itr;
						if (aWindow != this)
						{
							LONG myStyle = ::GetWindowLong(mHWnd, GWL_EXSTYLE);
							LONG targetStyle = ::GetWindowLong(aWindow->mHWnd, GWL_EXSTYLE);
							if ((targetStyle & WS_EX_TRANSPARENT) == 0)
							{
								if (aWindow->mHWnd == windowAtPoint)
								{
									POINT clientPt = point;
									::ScreenToClient(aWindow->mHWnd, &clientPt);
									aWindow->mMouseProxyMoveFunc(aWindow, clientPt.x, clientPt.y);
									aWindow->mIsMouseInside = true;
								}
								else if (aWindow->mIsMouseInside)
								{
									aWindow->mMouseLeaveFunc(aWindow);
									aWindow->mIsMouseInside = false;
								}
							}
						}
						++itr;
					}
				}

				if (releaseCapture)
				{
					for (int i = 0; i < MOUSEBUTTON_MAX; i++)
						if (mIsMouseDown[i])
							releaseCapture = false;
				}

				if (releaseCapture)
				{
					//OutputDebugStrF("Wnd %p Release Capture\n", this);

					//OutputDebugStrF("ReleaseCapture\n");
					ReleaseCapture();

					BFWindowList::iterator itr = gBFApp->mWindowList.begin();
					while (itr != gBFApp->mWindowList.end())
					{
						WinBFWindow* aWindow = (WinBFWindow*) *itr;
						if ((aWindow != this) && (aWindow->mIsMouseInside))
						{
							aWindow->mMouseLeaveFunc(aWindow);
							aWindow->mIsMouseInside = false;
						}
						++itr;
					}
				}
			}
			break;

		case WM_COMMAND:
			{
				WinBFMenu* aMenu = (*menuIDMap)[(uint32)wParam];
				if (aMenu != NULL)
					menuTarget->mMenuItemSelectedFunc(menuTarget, aMenu);
			}
			break;
		case WM_APPCOMMAND:
			{
				if ((mFlags & BFWINDOW_CAPTURE_MEDIA_KEYS) != 0)
				{
					int cmd = GET_APPCOMMAND_LPARAM(lParam);
					int uDevice = GET_DEVICE_LPARAM(lParam);
					int dwKeys = GET_KEYSTATE_LPARAM(lParam);
					int key = cmd | 0x1000;
					mKeyDownFunc(this, key, false);
					result = TRUE;
					doResult = true;
				}
			}
			break;
		case WM_INITMENUPOPUP:
			{
				if (mIsKeyDown[VK_MENU])
				{
					mKeyUpFunc(this, (int)VK_MENU);
					mIsKeyDown[VK_MENU] = false;
				}

				HMENU hMenu = (HMENU) wParam;
				WinBFMenu* aMenu = (*hMenuMap)[hMenu];
				if (aMenu != NULL)
					menuTarget->mMenuItemSelectedFunc(menuTarget, aMenu);
			}
			break;

		case WM_MOUSEACTIVATE:
			if (mFlags & BFWINDOW_NO_MOUSE_ACTIVATE)
			{
				doResult = true;
				result = MA_NOACTIVATE;
			}
			else if (mFlags & BFWINDOW_FAKEFOCUS)
			{
				doResult = true;
				result = MA_NOACTIVATE;
				SetForeground();
			}
			break;
		case WM_ACTIVATE:
			//OutputDebugStrF("WM_ACTIVATE %p\n", hWnd);
			break;
		case WM_KILLFOCUS:
			//OutputDebugStrF("WM_KILLFOCUS %p\n", hWnd);
			mHasFocus = false;
			mSoftHasFocus = false;
			LostFocus(NULL);
			mLostFocusFunc(this);
			break;
		case WM_SETFOCUS:
			//OutputDebugStrF("WM_SETFOCUS %p\n", hWnd);
			if (!mHasFocus)
				GotFocus();
			mHasFocus = true;
			mSoftHasFocus = true;
			mGotFocusFunc(this);
			break;
		case WM_ENTERMENULOOP:
			//OutputDebugStrF("WM_ENTERMENULOOP %08X\n", hWnd);
			if (mMenu != NULL)
				mNeedsStateReset = true;
			break;
		case WM_EXITMENULOOP:
			//OutputDebugStrF("WM_EXITMENULOOP %08X\n", hWnd);
			if (mMenu != NULL)
				mNeedsStateReset = true;
			break;
		case WM_NCMOUSELEAVE:
			mIsMouseInside = false;
			mMouseLeaveFunc(this);
			break;

		case WM_CHAR:
			{
				/*if (wParam == 'z')
				{
					ID3D11Debug* pD3DDebug = NULL;
					HRESULT hr =  ((DXRenderDevice*)mRenderWindow->mRenderDevice)->mD3DDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pD3DDebug);
					if (pD3DDebug != NULL)
					{
						pD3DDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
						pD3DDebug->Release();
					}
				}*/

				//NOTE: This line broke Alt+Gr for braces and such. Determine why this was needed.
				//if ((!mIsKeyDown[VK_MENU]) && (!mIsKeyDown[VK_CONTROL]))
				if (CheckKeyReleases(true))
				{
					for (int i = 0; i < (lParam & 0x7FFF); i++)
						mKeyCharFunc(this, (WCHAR)wParam);
				}
			}
			break;
		case WM_MENUCHAR:
			if (mIsMenuKeyHandled)
			{
				result = MNC_CLOSE << 16;
				doResult = true;
			}
			break;

		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			{
				mIsMenuKeyHandled = false;
				int keyCode = (int) wParam;
				if (keyCode == VK_APPS)
					break; // This is handled in WM_CONTEXTMENU

				if ((mKeyLayoutHasAltGr) && (keyCode == VK_MENU) && ((lParam & 0x01000000) != 0))
					keyCode = VK_RMENU;

				mIsKeyDown[keyCode] = true;

// 				if ((keyCode == 192) && (mIsKeyDown[VK_MENU]))
// 				{
// 					((DXRenderDevice*)mRenderWindow->mRenderDevice)->mNeedsReinitNative = true;
// 				}

				for (auto kv : *menuIDMap)
				{
					WinBFMenu* aMenu = kv.mValue;
					if ((aMenu->mKeyCode == keyCode) &&
						(aMenu->mKeyShift == mIsKeyDown[VK_SHIFT]) &&
						(aMenu->mKeyCtrl == mIsKeyDown[VK_CONTROL]) &&
						(aMenu->mKeyAlt == mIsKeyDown[VK_MENU]))
					{
						mIsMenuKeyHandled = true;
						menuTarget->mMenuItemSelectedFunc(menuTarget, aMenu);
						doResult = true;
						break;
					}
				}

				if (!mIsMenuKeyHandled)
				{
					if ((CheckKeyReleases(true)) && (mKeyDownFunc(this, keyCode, (lParam & 0x7FFF) != 0)))
					{
						mIsMenuKeyHandled = true;
						doResult = true;
					}
				}
			}
			break;
		case WM_SYSCHAR:
			{
				int keyCode = toupper((int) wParam);

				for (auto& menuKV : *menuIDMap)
				{
					WinBFMenu* aMenu = menuKV.mValue;
					if ((aMenu->mKeyCode == keyCode) &&
						(aMenu->mKeyShift == mIsKeyDown[VK_SHIFT]) &&
						(aMenu->mKeyCtrl == mIsKeyDown[VK_CONTROL]) &&
						(aMenu->mKeyAlt == mIsKeyDown[VK_MENU]))
					{
						if (CheckKeyReleases(true))
							doResult = true;
						break;
					}
				}

				if (!mIsKeyDown[VK_MENU])
				{
					// If we don't have the alt key down then we assume we must have
					//  had keyups forced by losing focus from the mMenuItemSelectedFunc
					doResult = true;
				}
			}
			break;
		case WM_SYSKEYUP:
		case WM_KEYUP:
			{
				int keyCode = (int) wParam;
				if ((mKeyLayoutHasAltGr) && (keyCode == VK_MENU) && ((lParam & 0x01000000) != 0))
					keyCode = VK_RMENU;
				if (mIsKeyDown[keyCode])
				{
					mKeyUpFunc(this, keyCode);
					mIsKeyDown[keyCode] = false;
				}
				CheckKeyReleases(false);
			}
			break;
		case WM_SYSCOMMAND:
			// Ignore F10
			if ((wParam == SC_KEYMENU) && (lParam == 0))
			{
				doResult = true;
				result = 0;
			}
			break;
		case WM_CONTEXTMENU:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				if ((x == -1) && (y == -1))
				{
					mKeyDownFunc(this, VK_APPS, false);
					mKeyUpFunc(this, VK_APPS);
				}
			}
			break;
		case WM_TIMER:
			if (gBFApp->mSysDialogCnt == 0)
			{
				auto checkNonFake = this;
				while (checkNonFake->mFlags & BFWINDOW_FAKEFOCUS)
				{
					checkNonFake = (WinBFWindow*)checkNonFake->mParent;
				}
				bool isFocused = GetForegroundWindow() == checkNonFake->mHWnd;

				if ((!isFocused) && (mHasFocus))
				{
					mSoftHasFocus = false;
					mHasFocus = false;
					LostFocus(NULL);
					mLostFocusFunc(this);
					//OutputDebugStrF("Timer detected lost focus %p\r\n", hWnd);
				}
				else if ((isFocused) && (!mHasFocus) && (checkNonFake == this))
				{
					//OutputDebugStrF("Timer detected got focus %p\r\n", hWnd);
					GotFocus();
					mHasFocus = true;
					mSoftHasFocus = true;
					mGotFocusFunc(this);
				}

				mSoftHasFocus = mHasFocus;
				gBFApp->Process();
				// Don't do anything with 'this' after Process, we may be deleted now
				doResult = true;
				result = 0;
			}
			break;

		case WM_SETCURSOR:
			gBFApp->PhysSetCursor();
			break;

		case WM_GETMINMAXINFO:
			{
				MINMAXINFO* minMaxInfo = (MINMAXINFO*)lParam;
				minMaxInfo->ptMinTrackSize.x = mMinWidth;
				minMaxInfo->ptMinTrackSize.y = mMinHeight;
				result = 0;
				doResult = true;
			}
			break;

		case WM_MOVE:
		case WM_MOVING:
			if (mMovedFunc != NULL)
				mMovedFunc(this);
			break;
		case WM_SIZING:
			mRenderWindow->Resized();
			if (mMovedFunc != NULL)
				mMovedFunc(this);
			if (gBFApp->mSysDialogCnt == 0)
				gBFApp->Process();
			break;

		case WM_INPUTLANGCHANGE:
			mKeyLayoutHasAltGr = (KeyboardLayoutHasAltGr((HKL)lParam) == TRUE);
			break;

		case WM_DROPFILES:
			{
				HDROP hDropInfo = (HDROP)wParam;
				char sItem[MAX_PATH];

				for(int i = 0; DragQueryFileA(hDropInfo, i, (LPSTR)sItem, sizeof(sItem)); i++)
				    mDragDropFileFunc(this, sItem);

				DragFinish(hDropInfo);
			}
			break;
		}

		app->mInMsgProc = false;
	}
	else
	{
		// We got messages we couldn't process (due to reentrancy)
		switch (uMsg)
		{
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_MOUSEMOVE:
		case WM_KEYUP:
		case WM_MOUSELEAVE:
			mNeedsStateReset = true;
			break;
		}
	}

	if (doResult)
		return result;

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WinBFWindow::WindowProcStub(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	WinBFWindow* aWindow = (WinBFWindow*) GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (aWindow != NULL)
		return aWindow->WindowProc(hWnd, Msg, wParam, lParam);
	else
		return DefWindowProc(hWnd, Msg, wParam, lParam);
}

//


static int WinBFReportHook( int reportType, char *message, int *returnValue )
{
	if (reportType == 0)
		return 0;

	//__crtMessageWindowW(nRptType, szFile, (nLine ? szLineMessage : NULL), szModule, szUserMessage);;
	if (gBFApp)
		((WinBFApp*) gBFApp)->mInMsgProc = true;
	int nCode = ::MessageBoxA(NULL, message,
                             "Microsoft Visual C++ Debug Library",
                             MB_TASKMODAL|MB_ICONHAND|MB_ABORTRETRYIGNORE|MB_SETFOREGROUND);
	if (gBFApp)
		((WinBFApp*) gBFApp)->mInMsgProc = false;
	/* Abort: abort the program */
    if (IDABORT == nCode)
    {
        /* note that it is better NOT to call abort() here, because the
            * default implementation of abort() will call Watson
            */

        /* raise abort signal */
        raise(SIGABRT);

        /* We usually won't get here, but it's possible that
            SIGABRT was ignored.  So exit the program anyway. */
        _exit(3);
    }

    /* Retry: return 1 to call the debugger */
    if (IDRETRY == nCode)
        return 1;

    /* Ignore: continue execution */
    return 0;




	return 1;
}

extern HINSTANCE gDLLInstance;

typedef UINT(NTAPI *GetDpiForWindow_t)(HWND);
static GetDpiForWindow_t gGetDpiForWindow = NULL;
static HMODULE gUserDll = NULL;

WinBFApp::WinBFApp()
{
#ifndef BF_MINGW
	//_CrtSetReportHook(WinBFReportHook);
#endif

	if (gUserDll == NULL)
	{
		gUserDll = ::LoadLibraryA("user32.dll");
		gGetDpiForWindow = (GetDpiForWindow_t)::GetProcAddress(gUserDll, "GetDpiForWindow");
	}

	mRunning = false;
	mRenderDevice = NULL;

	mInstallDir = "Hey";

	WCHAR aStr[MAX_PATH];
	GetModuleFileNameW(gDLLInstance, aStr, MAX_PATH);
	mInstallDir = UTF8Encode(aStr);

	int a2DArray[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};

	int lastSlash = (int) mInstallDir.LastIndexOf(L'\\');
	if (lastSlash != -1)
		mInstallDir = mInstallDir.Substring(0, lastSlash + 1);

	mDataDir = mInstallDir;
	mInMsgProc = false;
	mDSoundManager = NULL;
	mDInputManager = NULL;

	mVSyncThreadId = 0;
	mClosing = false;
	mVSyncActive = false;
	mVSyncThread = BfpThread_Create(VSyncThreadProcThunk, (void*)this, 128 * 1024, BfpThreadCreateFlag_StackSizeReserve, &mVSyncThreadId);
	BfpThread_SetPriority(mVSyncThread, BfpThreadPriority_High, NULL);
}

void BFP_CALLTYPE WinBFApp::VSyncThreadProcThunk(void* ptr)
{
	((WinBFApp*)ptr)->VSyncThreadProc();
}

void WinBFApp::VSyncThreadProc()
{
	DWORD lastBlankFinish = GetTickCount();

	Array<int> waitTimes;

	while (!mClosing)
	{
		bool didWait = false;

		IDXGIOutput* output = NULL;

		//
		{
			AutoCrit autoCrit(mCritSect);
			if ((mRenderDevice != NULL) && (!mRenderDevice->mRenderWindowList.IsEmpty()))
			{
				auto renderWindow = (DXRenderWindow*)mRenderDevice->mRenderWindowList[0];
				renderWindow->mDXSwapChain->GetContainingOutput(&output);
			}
		}

		if (output != NULL)
		{
			DWORD startTick = GetTickCount();
			bool success = output->WaitForVBlank() == 0;
			DWORD endTick = GetTickCount();

			if (success)
			{
				int elapsed = (int)(endTick - startTick);
				waitTimes.Add(elapsed);
				if (waitTimes.mSize > 8)
					waitTimes.RemoveAt(0);

				if (elapsed <= 1)
				{
					bool hadNonZero = false;
					for (auto waitTime : waitTimes)
					{
						if (waitTime > 1)
							hadNonZero = true;
					}
					if (!hadNonZero)
						success = false;
				}
			}

			if (success)
			{
				didWait = true;
				mVSyncActive = true;
				mVSyncEvent.Set();
			}

			output->Release();
		}

		if (!didWait)
		{
			mVSyncActive = false;
			BfpThread_Sleep(20);
		}
	}
}

WinBFApp::~WinBFApp()
{
	mClosing = true;
	BfpThread_WaitFor(mVSyncThread, -1);
	BfpThread_Release(mVSyncThread);

	delete mRenderDevice;
	delete mDSoundManager;
	delete mDInputManager;
}

void WinBFApp::Init()
{
	BP_ZONE("WinBFApp::Init");

	AutoCrit autoCrit(mCritSect);

	mRunning = true;
	mInMsgProc = false;

	mRenderDevice = new DXRenderDevice();
	mRenderDevice->Init(this);
}

void WinBFApp::Run()
{
	MSG msg;
	while (mRunning)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (mRunning)
			Process();
	}
}

void WinBFApp::Process()
{
	BFApp::Process();

	auto dxRenderDevice = (DXRenderDevice*)mRenderDevice;
	if (dxRenderDevice->mNeedsReinitNative)
	{
		dxRenderDevice->mNeedsReinitNative = false;
		dxRenderDevice->ReinitNative();
		mForceNextDraw = true;
	}
}

void WinBFApp::Draw()
{
	mRenderDevice->FrameStart();
	BFApp::Draw();
	mRenderDevice->FrameEnd();
}

void WinBFApp::GetDesktopResolution(int& width, int& height)
{
	width = ::GetSystemMetrics(SM_CXSCREEN);
	height = ::GetSystemMetrics(SM_CYSCREEN);
}

static BOOL InflateRectToMonitor(HMONITOR mon, HDC hdc, LPRECT monRect, LPARAM userArg)
{
	AdjustedMonRect* inflatedRect = (AdjustedMonRect*)userArg;

	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (::GetMonitorInfo(mon, &monitorInfo) == 0)
		return TRUE;

	inflatedRect->mMonCount++;
	if (inflatedRect->mMonCount == 1)
	{
		inflatedRect->mX = monitorInfo.rcWork.left;
		inflatedRect->mY = monitorInfo.rcWork.top;
		inflatedRect->mWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
		inflatedRect->mHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
	}
	else
	{
		int minLeft = BF_MIN(inflatedRect->mX, monitorInfo.rcWork.left);
		int minTop = BF_MIN(inflatedRect->mY, monitorInfo.rcWork.top);
		int maxRight = BF_MAX(inflatedRect->mX + inflatedRect->mWidth, monitorInfo.rcWork.right);
		int maxBottom = BF_MAX(inflatedRect->mY + inflatedRect->mHeight, monitorInfo.rcWork.bottom);

		inflatedRect->mX = minLeft;
		inflatedRect->mY = minTop;
		inflatedRect->mWidth = maxRight - minLeft;
		inflatedRect->mHeight = maxBottom - minTop;
	}

	return TRUE;
}

void WinBFApp::GetWorkspaceRect(int& x, int& y, int& width, int& height)
{
	AdjustedMonRect inflateRect = { 0 };

	EnumDisplayMonitors(NULL, NULL, InflateRectToMonitor, (LPARAM)&inflateRect);
	x = inflateRect.mX;
	y = inflateRect.mY;
	width = inflateRect.mWidth;
	height = inflateRect.mHeight;
}

void WinBFApp::GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, int & outX, int & outY, int & outWidth, int & outHeight)
{
	AdjustedMonRect inflateRect = { 0 };

	RECT wantRect;
	wantRect.left = fromX;
	wantRect.top = fromY;
	wantRect.right = fromX + BF_MAX(fromWidth, 1);
	wantRect.bottom = fromY + BF_MAX(fromHeight, 1);
	EnumDisplayMonitors(NULL, &wantRect, InflateRectToMonitor, (LPARAM)&inflateRect);

	if (inflateRect.mMonCount == 0)
	{
		GetWorkspaceRect(outX, outY, outWidth, outHeight);
		return;
	}

	outX = inflateRect.mX;
	outY = inflateRect.mY;
	outWidth = inflateRect.mWidth;
	outHeight = inflateRect.mHeight;
}

BFWindow* WinBFApp::CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags)
{
	AutoCrit autoCrit(mCritSect);

	BFWindow* aWindow = new WinBFWindow(parent, title, x, y, width, height, windowFlags);
	mWindowList.push_back(aWindow);

	return aWindow;
}

void WinBFApp::RehupMouse()
{
	HWND windowAtPoint = ::WindowFromPoint(gLastScreenMouseCoords);
	for (auto window : mWindowList)
	{
		auto winWindow = (WinBFWindow*)window;
		winWindow->RehupMouseOver(winWindow->mHWnd == windowAtPoint);
	}
}

String WinBFApp::EnumerateInputDevices()
{
	if (mDInputManager == NULL)
		mDInputManager = new DInputManager();
	return mDInputManager->EnumerateDevices();
}

BFInputDevice* WinBFApp::CreateInputDevice(const StringImpl& guid)
{
	if (mDInputManager == NULL)
		mDInputManager = new DInputManager();
	return mDInputManager->CreateInputDevice(guid);
}

void WinBFWindow::SetMinimumSize(int minWidth, int minHeight, bool clientSized)
{
	if (clientSized)
	{
		DWORD windowFlags = ::GetWindowLong(mHWnd, GWL_STYLE);
		DWORD windowFlagsEx = ::GetWindowLong(mHWnd, GWL_EXSTYLE);

		RECT rect = { 0, 0, minWidth, minHeight };
		AdjustWindowRectEx(&rect, windowFlags, mMenu != NULL, windowFlagsEx);
		minWidth = rect.right - rect.left;
		minHeight = rect.bottom - rect.top;
	}

	mMinWidth = minWidth;
	mMinHeight = minHeight;

	if (mHWnd != NULL)
	{
		RECT windowRect;
		::GetWindowRect(mHWnd, &windowRect);

		bool resized = false;

		if (windowRect.right - windowRect.left < minWidth)
		{
			windowRect.right = windowRect.left + minWidth;
			resized = true;
		}

		if (windowRect.bottom - windowRect.top < minHeight)
		{
			windowRect.bottom = windowRect.top + minHeight;
			resized = true;
		}

		if (resized)
		{
			::MoveWindow(mHWnd, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, TRUE);
			mRenderWindow->Resized();
			if (mMovedFunc != NULL)
				mMovedFunc(this);
		}
	}
}

void WinBFWindow::GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight)
{
	RECT windowRect;
	::GetWindowRect(mHWnd, &windowRect);

	RECT clientRect;
	::GetClientRect(mHWnd, &clientRect);

	if (clientRect.right <= clientRect.left)
		return; // TODO: return failure?

	*x = windowRect.left;
	*y = windowRect.top;
	*width = windowRect.right - windowRect.left;
	*height = windowRect.bottom - windowRect.top;
	*clientWidth = clientRect.right - clientRect.left;
	*clientHeight = clientRect.bottom - clientRect.top;

	POINT startPt = {0, 0};
	::ClientToScreen(mHWnd, &startPt);
	*clientX = startPt.x;
	*clientY = startPt.y;
}

void WinBFWindow::GetPlacement(int* normX, int* normY, int* normWidth, int* normHeight, int* showKind)
{
	WINDOWPLACEMENT wndPlacement = { sizeof(WINDOWPLACEMENT), 0 };
	::GetWindowPlacement(mHWnd, &wndPlacement);
	*normX = wndPlacement.rcNormalPosition.left;
	*normY = wndPlacement.rcNormalPosition.top;
	*normWidth = wndPlacement.rcNormalPosition.right - wndPlacement.rcNormalPosition.left;
	*normHeight = wndPlacement.rcNormalPosition.bottom - wndPlacement.rcNormalPosition.top;
	switch (wndPlacement.showCmd)
	{
	case SW_SHOWNORMAL:
		*showKind = ShowKind_ShowNormal;
		break;
	case SW_SHOWMINIMIZED:
		*showKind = ShowKind_ShowMinimized;
		break;
	case SW_SHOWMAXIMIZED:
		*showKind = ShowKind_ShowMaximized;
		break;
	default:
		*showKind = ShowKind_Hide;
		break;
	}
}

void WinBFWindow::Resize(int x, int y, int width, int height, ShowKind showKind)
{
	WINDOWPLACEMENT wndPlacement = { sizeof(WINDOWPLACEMENT), 0 };
	::GetWindowPlacement(mHWnd, &wndPlacement);

	wndPlacement.showCmd = ToWShow(showKind);
	wndPlacement.rcNormalPosition.left = x;
	wndPlacement.rcNormalPosition.top = y;
	wndPlacement.rcNormalPosition.right = x + width;
	wndPlacement.rcNormalPosition.bottom = y + height;
	::SetWindowPlacement(mHWnd, &wndPlacement);

	//::MoveWindow(mHWnd, x, y, width, height, FALSE);
	mRenderWindow->Resized();
	if (mMovedFunc != NULL)
		mMovedFunc(this);
}

void WinBFApp::PhysSetCursor()
{
	static HCURSOR cursors [] =
		{
			::LoadCursor(NULL, IDC_ARROW),

			//TODO: mApp->mHandCursor);
			::LoadCursor(NULL, IDC_HAND),
			//TODO: mApp->mDraggingCursor);
			::LoadCursor(NULL, IDC_SIZEALL),

			::LoadCursor(NULL, IDC_IBEAM),

			::LoadCursor(NULL, IDC_NO),
			::LoadCursor(NULL, IDC_SIZEALL),
			::LoadCursor(NULL, IDC_SIZENESW),
			::LoadCursor(NULL, IDC_SIZENS),
			::LoadCursor(NULL, IDC_SIZENWSE),
			::LoadCursor(NULL, IDC_SIZEWE),
			::LoadCursor(NULL, IDC_WAIT),
			NULL
		};

	::SetCursor(cursors[mCursor]);
}

void WinBFWindow::SetClientPosition(int x, int y)
{
	RECT rect;
	::GetClientRect(mHWnd, &rect);

	::OffsetRect(&rect, x, y);

	LONG aStyle = ::GetWindowLong(mHWnd, GWL_STYLE);
	LONG exStyle = ::GetWindowLong(mHWnd, GWL_EXSTYLE);
	::AdjustWindowRectEx(&rect, aStyle, mMenu != NULL, exStyle);

	::MoveWindow(mHWnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, FALSE);

	if (mMovedFunc != NULL)
		mMovedFunc(this);
}

void WinBFWindow::SetMouseVisible(bool isMouseVisible)
{
	mMouseVisible = isMouseVisible;
	LONG aStyle = ::GetWindowLong(mHWnd, GWL_EXSTYLE);
	if (!isMouseVisible)
		aStyle |= WS_EX_TRANSPARENT;
	else
		aStyle &= ~WS_EX_TRANSPARENT;
	::SetWindowLong(mHWnd, GWL_EXSTYLE, aStyle);
}

void WinBFWindow::SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible)
{
	if (destAlphaSrcMask != 0)
	{
		if (mAlphaMaskBitmap == NULL)
		{
			RECT clientRect;
			GetClientRect(mHWnd, &clientRect);

			RECT windowRect;
			GetWindowRect(mHWnd, &windowRect);

			int aWidth = clientRect.right - clientRect.left;
			int aHeight = clientRect.bottom - clientRect.top;
			mAlphaMaskWidth = aWidth;
			mAlphaMaskHeight = aHeight;

			mAlphaMaskDC = CreateCompatibleDC(NULL);

			BITMAPINFO bi = {};
			bi.bmiHeader.biSize				= sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biBitCount			= 32;
			bi.bmiHeader.biWidth			= aWidth;
			bi.bmiHeader.biHeight			= -aHeight;
			bi.bmiHeader.biCompression		= BI_RGB;
			bi.bmiHeader.biPlanes			= 1;

			mAlphaMaskBitmap = CreateDIBSection(mAlphaMaskDC, &bi,
				DIB_RGB_COLORS, (void**)&mAlphaMaskPixels, NULL, 0);

			GdiFlush();
		}

		HDC hdc = GetDC(mHWnd);
		if (hdc)
		{
			DXRenderWindow* renderWindow = (DXRenderWindow*) mRenderWindow;
			renderWindow->CopyBitsTo(mAlphaMaskPixels, mAlphaMaskWidth, mAlphaMaskHeight);

			//for (int i = 0; i < mAlphaMaskWidth*mAlphaMaskHeight/2; i++)
				//mAlphaMaskPixels[i] = 0x80FF8000;

			HGDIOBJ hPrevObj = 0;
			POINT ptDest = {0, 0};
			POINT ptSrc = {0, 0};
			SIZE client = {mAlphaMaskWidth, mAlphaMaskHeight};
			BLENDFUNCTION blendFunc = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

			hPrevObj = SelectObject(mAlphaMaskDC, mAlphaMaskBitmap);
			ClientToScreen(mHWnd, &ptDest);

			BOOL worked = ::UpdateLayeredWindow(mHWnd, hdc, NULL, &client,
				mAlphaMaskDC, &ptSrc, 0, &blendFunc, ULW_ALPHA);

			DWORD error = GetLastError();

			SelectObject(mAlphaMaskDC, hPrevObj);
			ReleaseDC(mHWnd, hdc);
		}
	}
	else
	{
		::SetLayeredWindowAttributes(mHWnd, 0, (int) (alpha * 255), LWA_ALPHA);
	}
	SetMouseVisible(isMouseVisible);
}

void WinBFWindow::CaptureMouse()
{
	//OutputDebugStrF("Wnd %p CaptureMouse", this);
	::SetCapture(mHWnd);

	for (auto window : gBFApp->mWindowList)
	{
		if (window == this)
			continue;

		for (int i = 0; i < MOUSEBUTTON_MAX; i++)
		{
			if (window->mIsMouseDown[i])
			{
				window->mMouseUpFunc(window, -1, -1, i);
				window->mIsMouseDown[i] = false;
			}
		}
	}

}

bool WinBFWindow::IsMouseCaptured()
{
	return (mHWnd != NULL) && (GetCapture() == mHWnd);
}

int WinBFWindow::GetDPI()
{
	if (gGetDpiForWindow != NULL)
		return (int)gGetDpiForWindow(mHWnd);
	return 96; // Default DPI
}

uint32 WinBFApp::GetClipboardFormat(const StringImpl& format)
{
	if (format == "text")
		return CF_UNICODETEXT;
	else if (format == "atext")
		return CF_TEXT;

	uint32 aFormat;
	if (mClipboardFormatMap.TryGetValue(format, &aFormat))
		return aFormat;

	aFormat = ::RegisterClipboardFormatA(format.c_str());
	mClipboardFormatMap[format] = aFormat;
	return aFormat;
}

static String gClipboardData;

void* WinBFApp::GetClipboardData(const StringImpl& format, int* size)
{
	HWND aWindow = NULL;
	if (!mWindowList.empty())
		aWindow = ((WinBFWindow*)mWindowList.front())->mHWnd;

	uint32 aFormat = GetClipboardFormat(format);

	if (aFormat != 0)
	{
		if (OpenClipboard(aWindow))
		{
			HGLOBAL globalHandle = ::GetClipboardData(aFormat);

			if (globalHandle == NULL)
			{
				if (aFormat == CF_UNICODETEXT)
				{
					CloseClipboard();
					// Return ascii text
					return (char*)GetClipboardData("atext", size);
				}

				CloseClipboard();
				*size = 0;
				return NULL;
			}

			*size = (int)::GlobalSize(globalHandle);
			void* aPtr = ::GlobalLock(globalHandle);

			if (aFormat == CF_UNICODETEXT)
			{
				gClipboardData = UTF8Encode((WCHAR*)aPtr);
				*size = (int)gClipboardData.length() + 1;
			}
			else
			{
				gClipboardData.Clear();
				gClipboardData.Insert(0, (char*)aPtr, *size);
			}

			::GlobalUnlock(globalHandle);
			CloseClipboard();
			return (void*)gClipboardData.c_str();
		}
		else
		{
			*size = -1;
			return NULL;
		}
	}

	*size = 0;
	return NULL;
}

void WinBFApp::ReleaseClipboardData(void* ptr)
{

}

void WinBFApp::SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard)
{
	BP_ZONE("WinBFApp::SetClipboardData");

	HWND aWindow = NULL;
	if (!mWindowList.empty())
		aWindow = ((WinBFWindow*) mWindowList.front())->mHWnd;

	uint32 aFormat = GetClipboardFormat(format);

	if (aFormat != 0)
	{
		if (OpenClipboard(aWindow))
		{
			if (resetClipboard)
			{
				BP_ZONE("WinBFApp::SetClipboardData:empty");
				EmptyClipboard();
			}

			if (format == "text")
			{
				BP_ZONE("WinBFApp::SetClipboardData:text");

				HGLOBAL globalHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size * 2);
				char* data = (char*)GlobalLock(globalHandle);
				UTF16String wString;
				//
				{
					BP_ZONE("WinBFApp::SetClipboardData:utf8decode");
					wString = UTF8Decode((char*)ptr);
				}
				memcpy(data, wString.c_str(), size * 2);
				GlobalUnlock(globalHandle);
				::SetClipboardData(CF_UNICODETEXT, globalHandle);
			}
			else
			{
				HGLOBAL globalHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
				char* data = (char*)GlobalLock(globalHandle);
				memcpy(data, ptr, size);
				GlobalUnlock(globalHandle);
				::SetClipboardData(aFormat, globalHandle);
			}

			CloseClipboard();
		}
	}
}

BFMenu* WinBFWindow::AddMenuItem(BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* sysBitmap, bool enabled, int checkState, bool radioCheck)
{
	UTF16String lText;
	if (text != NULL)
		lText = UTF8Decode(text);
	UTF16String lHotKey;
	if (hotKey != NULL)
		lHotKey = UTF8Decode(hotKey);

	bool wasEmpty = mMenu->mBFMenuList.size() == 0;

	if (parent == NULL)
		parent = mMenu;

	if ((parent->mBFMenuList.size() == 1) && (((WinBFMenu*) parent->mBFMenuList.front())->mIsPlaceholder))
	{
		// Get rid of placeholder menu item
		auto placeholderMenuItem = parent->mBFMenuList.front();
		RemoveMenuItem(placeholderMenuItem);
		delete placeholderMenuItem;
	}

	WinBFMenu* winBFMenuParent = (WinBFMenu*) parent;
	WinBFMenu* newMenu = new WinBFMenu();
	newMenu->mMenuId = ++WinBFMenu::mMenuCount;
	newMenu->mParent = parent;

	//static int allocIdx = 0;
	//allocIdx++;
	//OutputDebugStrF("MenuIdx %d %@\n", allocIdx, newMenu);

	if ((winBFMenuParent != NULL) && (winBFMenuParent->mMenu == NULL))
	{
		winBFMenuParent->mMenu = ::CreateMenu();
		mHMenuMap[winBFMenuParent->mMenu] = winBFMenuParent;

		MENUITEMINFO menuItem;
		memset(&menuItem, 0, sizeof(MENUITEMINFO));
		menuItem.cbSize = sizeof(MENUITEMINFO);
		menuItem.fMask = MIIM_SUBMENU;
		menuItem.hSubMenu = winBFMenuParent->mMenu;

		::SetMenuItemInfo(((WinBFMenu*) winBFMenuParent->mParent)->mMenu, winBFMenuParent->mMenuId, FALSE, &menuItem);
	}

	mMenuIDMap[newMenu->mMenuId] = newMenu;

	BF_ASSERT(insertIdx <= (int) parent->mBFMenuList.size());

	////
	UTF16String lCombinedName;

	MENUITEMINFOW menuItem;
	memset(&menuItem, 0, sizeof(MENUITEMINFO));
	menuItem.cbSize = sizeof(MENUITEMINFO);
	menuItem.fMask = MIIM_FTYPE | MIIM_ID;
	if (text != NULL)
	{
		menuItem.fMask |= MIIM_STRING;
		menuItem.fType = MFT_STRING;
	}
	else
		menuItem.fType = MFT_SEPARATOR;
	menuItem.fState = enabled ? MFS_DEFAULT : MFS_GRAYED;
	if (checkState == 0)
		menuItem.fState |= MFS_UNCHECKED;
	if (checkState == 1)
		menuItem.fState |= MFS_CHECKED;
	if (radioCheck)
		menuItem.fType = MFT_RADIOCHECK;

	menuItem.wID = newMenu->mMenuId;
	if (text != NULL)
	{
		menuItem.dwTypeData = (WCHAR*)lText.c_str();
	}

	if (hotKey != NULL)
	{
		String combinedName = String(text);
		combinedName += "\t";
		if (hotKey[0] == '#')
		{
			combinedName += hotKey + 1;
		}
		else
		{
			combinedName += hotKey;
			newMenu->ParseHotKey(hotKey);
		}
		lCombinedName = UTF8Decode(combinedName);
		menuItem.dwTypeData = (WCHAR*)lCombinedName.c_str();
	}
	::InsertMenuItemW(winBFMenuParent->mMenu, insertIdx, TRUE, &menuItem);
	////////

	/*MENUITEMINFO menuItem;
	memset(&menuItem, 0, sizeof(MENUITEMINFO));
	menuItem.cbSize = sizeof(MENUITEMINFO);
	menuItem.fMask = MIIM_ID;
	menuItem.wID = newMenu->mMenuId;
	::InsertMenuItem(winBFMenuParent->mMenu, insertIdx, TRUE, &menuItem);*/

	ModifyMenuItem(newMenu, text, hotKey, sysBitmap, enabled, checkState, radioCheck);

	parent->mBFMenuList.push_back(newMenu);

	return newMenu;
}

void WinBFWindow::ModifyMenuItem(BFMenu* item, const char* text, const char* hotKey, BFSysBitmap* sysBitmap, bool enabled, int checkState, bool radioCheck)
{
	UTF16String lText;
	if (text != NULL)
		lText = UTF8Decode(text);
	UTF16String lHotKey;
	if (hotKey != NULL)
		lHotKey = UTF8Decode(hotKey);

	WinBFMenu* aMenu = (WinBFMenu*)item;
	WinBFMenu* parentMenu = (WinBFMenu*) item->mParent;

	UTF16String lCombinedName;

	MENUITEMINFOW menuItem;
	memset(&menuItem, 0, sizeof(MENUITEMINFO));
	menuItem.cbSize = sizeof(MENUITEMINFO);
	menuItem.fMask = MIIM_FTYPE | MIIM_ID;

	::GetMenuItemInfoW(parentMenu->mMenu, aMenu->mMenuId, FALSE, &menuItem);

	menuItem.fMask |= MIIM_STATE;

	if (text != NULL)
	{
		menuItem.fMask |= MIIM_STRING;
		//menuItem.fType = MFT_STRING;
	}
	/*else
		menuItem.fType = MFT_SEPARATOR;*/

	menuItem.fState = enabled ? /*MFS_DEFAULT*/0 : MFS_GRAYED;
	if (checkState != -1)
	{
		menuItem.fMask |= MIIM_CHECKMARKS;
	}
	if (checkState == 0)
		menuItem.fState |= MFS_UNCHECKED;
	if (checkState == 1)
		menuItem.fState |= MFS_CHECKED;
	if (radioCheck)
		menuItem.fType = MFT_RADIOCHECK;

	menuItem.wID = aMenu->mMenuId;
	menuItem.dwTypeData = (WCHAR*)lText.c_str();

	if (hotKey != NULL)
	{
		menuItem.fMask |= MIIM_DATA;

		String combinedName = String(text);
		combinedName += "\t";
		if (hotKey[0] == '#')
		{
			combinedName += hotKey + 1;
		}
		else
		{
			combinedName += hotKey;
			aMenu->ParseHotKey(hotKey);
		}
		lCombinedName = UTF8Decode(combinedName);
		menuItem.dwTypeData = (WCHAR*)lCombinedName.c_str();
	}

	::SetMenuItemInfoW(parentMenu->mMenu, aMenu->mMenuId, FALSE, &menuItem);
}

void WinBFWindow::RemoveMenuItem(BFMenu* item)
{
	WinBFMenu* aMenu = (WinBFMenu*) item;
	WinBFMenu* parentMenu = (WinBFMenu*) item->mParent;

	//auto itr = mMenuIDMap.find(aMenu->mMenuId);
	//mMenuIDMap.erase(itr);
	mMenuIDMap.Remove(aMenu->mMenuId);

	::RemoveMenu(parentMenu->mMenu, aMenu->mMenuId, MF_BYCOMMAND);
}

BFSysBitmap* WinBFApp::LoadSysBitmap(const WCHAR* fileName)
{
	return NULL;
}

BFSoundManager* WinBFApp::GetSoundManager()
{
	if (mDSoundManager == NULL)
		mDSoundManager = new DSoundManager(NULL);
	return mDSoundManager;
}

intptr WinBFApp::GetCriticalThreadId(int idx)
{
	if (idx == 0)
		return mVSyncThreadId;
	return 0;
}

void WinBFWindow::ModalsRemoved()
{
	::EnableWindow(mHWnd, TRUE);
	if (mChildren.empty())
		::SetFocus(mHWnd);
}

DrawLayer* WinBFApp::CreateDrawLayer(BFWindow* window)
{
	DXDrawLayer* drawLayer = new DXDrawLayer();
	if (window != NULL)
	{
		drawLayer->mRenderWindow = window->mRenderWindow;
		window->mRenderWindow->mDrawLayerList.push_back(drawLayer);
	}
	drawLayer->mRenderDevice = mRenderDevice;
	return drawLayer;
}

