#pragma once

#include "Common.h"

NS_BF_BEGIN;

class BFMenu;
class BFWindow;

typedef void (*BFWindow_MovedFunc)(BFWindow* window);
typedef int (*BFWindow_CloseQueryFunc)(BFWindow* window);
typedef void (*BFWindow_ClosedFunc)(BFWindow* window);
typedef void (*BFWindow_GotFocusFunc)(BFWindow* window);
typedef void (*BFWindow_LostFocusFunc)(BFWindow* window);
typedef void (*BFWindow_KeyCharFunc)(BFWindow* window, wchar_t theChar);
typedef bool (*BFWindow_KeyDownFunc)(BFWindow* window, int keyCode, int isRepeat);
typedef void (*BFWindow_KeyUpFunc)(BFWindow* window, int keyCode);
typedef int (*BFWindow_HitTestFunc)(BFWindow* window, int x, int y);
typedef void (*BFWindow_MouseMove)(BFWindow* window, int x, int y);
typedef void (*BFWindow_MouseProxyMove)(BFWindow* window, int x, int y);
typedef void (*BFWindow_MouseDown)(BFWindow* window, int x, int y, int btn, int btnCount);
typedef void (*BFWindow_MouseUp)(BFWindow* window, int x, int y, int btn);
typedef void (*BFWindow_MouseWheel)(BFWindow* window, int x, int y, float deltaX, float deltaY);
typedef void (*BFWindow_MouseLeave)(BFWindow* window);
typedef void (*BFWindow_MenuItemSelectedFunc)(BFWindow* window, BFMenu* menu);
typedef void (*BFWindow_DragDropFileFunc)(BFWindow* window, const char* filePath);

enum
{
	BFWINDOW_BORDER			= 0x000001,
	BFWINDOW_THICKFRAME		= 0x000002,
	BFWINDOW_RESIZABLE		= 0x000004,
	BFWINDOW_SYSMENU		= 0x000008,
	BFWINDOW_CAPTION		= 0x000010,
	BFWINDOW_MINIMIZE		= 0x000020,
	BFWINDOW_MAXIMIZE		= 0x000040,
	BFWINDOW_CLIENT_SIZED	= 0x000080,
	BFWINDOW_QUIT_ON_CLOSE	= 0x000100,
	BFWINDOW_VSYNC			= 0x000200,
	BFWINDOW_POPUP_POSITION = 0x000400,
	BFWINDOW_DEST_ALPHA		= 0x000800,
	BFWINDOW_ALPHA_MASK		= 0x0001000,
	BFWINDOW_CHILD			= 0x002000,
	BFWINDOW_TOPMOST		= 0x004000,
	BFWINDOW_TOOLWINDOW		= 0x008000,
	BFWINDOW_NO_ACTIVATE	= 0x010000,
	BFWINDOW_NO_MOUSE_ACTIVATE = 0x020000,
	BFWINDOW_MENU			= 0x040000,
	BFWINDOW_MODAL			= 0x080000,
	BFWINDOW_SCALE_CONTENT  = 0x100000,
	BFWINDOW_USE_PARENT_MENU = 0x200000,
	BFWINDOW_CAPTURE_MEDIA_KEYS = 0x400000,
	BFWINDOW_FULLSCREEN		= 0x800000,
	BFWINDOW_FAKEFOCUS		= 0x1000000,
	BFWINDOW_SHOWMINIMIZED  = 0x2000000,
	BFWINDOW_SHOWMAXIMIZED  = 0x4000000,
	BFWINDOW_ALLOW_FULLSCREEN = 0x8000000,
	BFWINDOW_ACCEPTFILES	= 0x10000000,
	BFWINDOW_NOSHOW			= 0x20000000

};

class RenderWindow;

class BFMenu
{
public:
	BFMenu*					mParent;
	Array<BFMenu*>			mBFMenuList;

	uint32					mKeyCode;
	bool					mKeyCtrl;
	bool					mKeyAlt;
	bool					mKeyShift;

public:
	BFMenu();
	virtual ~BFMenu() { }

	virtual bool			ParseHotKey(const StringImpl& hotKey);
};

class BFSysBitmap;

#define KEYCODE_MAX 0x100
#define MOUSEBUTTON_MAX 5

class BFWindow;

struct BFCoord
{
	int mX;
	int mY;
};

class BFWindow
{
public:
	enum ShowKind : int8
	{
		ShowKind_Hide,
		ShowKind_Normal,
		ShowKind_Minimized,
		ShowKind_Maximized,
		ShowKind_Show,
		ShowKind_ShowNormal,
		ShowKind_ShowMinimized,
		ShowKind_ShowMaximized
	};

public:
	BFWindow*				mParent;
	Array<BFWindow*>		mChildren;
	int						mFlags;
	bool					mIsKeyDown[KEYCODE_MAX];
	bool					mIsMouseDown[MOUSEBUTTON_MAX];
	BFCoord					mMouseDownCoords[MOUSEBUTTON_MAX];
	int						mMouseClickCount[MOUSEBUTTON_MAX];
	uint32					mMouseDownTicks[MOUSEBUTTON_MAX];

	BFMenu*					mMenu;
	RenderWindow*			mRenderWindow;
	bool					mNonExclusiveMouseCapture;
	BFWindow_MovedFunc		mMovedFunc;
	BFWindow_CloseQueryFunc mCloseQueryFunc;
	BFWindow_ClosedFunc		mClosedFunc;
	BFWindow_GotFocusFunc	mGotFocusFunc;
	BFWindow_LostFocusFunc	mLostFocusFunc;
	BFWindow_KeyCharFunc	mKeyCharFunc;
	BFWindow_KeyDownFunc	mKeyDownFunc;
	BFWindow_KeyUpFunc		mKeyUpFunc;
	BFWindow_HitTestFunc	mHitTestFunc;
	BFWindow_MouseMove		mMouseMoveFunc;
	BFWindow_MouseProxyMove	mMouseProxyMoveFunc;
	BFWindow_MouseDown		mMouseDownFunc;
	BFWindow_MouseUp		mMouseUpFunc;
	BFWindow_MouseWheel		mMouseWheelFunc;
	BFWindow_MouseLeave		mMouseLeaveFunc;
	BFWindow_MenuItemSelectedFunc mMenuItemSelectedFunc;
	BFWindow_DragDropFileFunc mDragDropFileFunc;

public:
	BFWindow();
	virtual ~BFWindow();

	virtual void*			GetUnderlying() = 0;
	virtual void			Destroy() = 0;
	virtual bool			TryClose() = 0;
	virtual void			SetTitle(const char* title) = 0;
	virtual void			SetMinimumSize(int minWidth, int minHeight, bool clientSized) = 0;
	virtual void			GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight) = 0;
	virtual void			GetPlacement(int* normX, int* normY, int* normWidth, int* normHeight, int* showKind) = 0;
	virtual void			Resize(int x, int y, int width, int height, ShowKind showKind) = 0;
	virtual void			SetClientPosition(int x, int y) = 0;
	virtual void			SetMouseVisible(bool isMouseVisible) = 0;
	virtual void			SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible) = 0;
	virtual void			Show(ShowKind showKind) = 0;
	virtual void			SetForeground() = 0;
	virtual void			SetNonExclusiveMouseCapture() { mNonExclusiveMouseCapture = true; }
	virtual void			CaptureMouse() {}
	virtual bool			IsMouseCaptured() { return false; }
	virtual void			LostFocus(BFWindow* newFocus) = 0;
	virtual int				GetDPI() { return 0; }

	virtual BFMenu*			AddMenuItem(BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) = 0;
	virtual void			ModifyMenuItem(BFMenu* item, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) = 0;
	virtual void			RemoveMenuItem(BFMenu* item) = 0;
	virtual void			ModalsRemoved() { }
};

NS_BF_END;
