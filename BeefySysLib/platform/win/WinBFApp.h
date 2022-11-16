#pragma once

#include "Common.h"
#include "BFApp.h"
#include "BFWindow.h"
#include "util/Dictionary.h"


NS_BF_BEGIN;

class RenderDevice;
class DSoundManager;
class DInputManager;

typedef Dictionary<void*, HGLOBAL> PtrToHGlobalMap;
typedef Dictionary<String, uint32> StringToUIntMap;

class WinBFMenu : public BFMenu
{
public:
	HMENU					mMenu;
	uint32					mMenuId;
	static int				mMenuCount;
	bool					mIsPlaceholder;

public:
	WinBFMenu();
};

typedef Dictionary<uint32, WinBFMenu*> WinMenuIDMap;
typedef Dictionary<HMENU, WinBFMenu*> WinHMenuMap;

class WinBFWindow : public BFWindow
{
public:
	HWND					mHWnd;
	bool					mIsMouseInside;
	WinMenuIDMap			mMenuIDMap;
	WinHMenuMap				mHMenuMap;

	int						mModalCount;
	int						mAlphaMaskWidth;
	int						mAlphaMaskHeight;
	HBITMAP					mAlphaMaskBitmap;
	HDC						mAlphaMaskDC;
	uint32*					mAlphaMaskPixels;
	bool					mIsMenuKeyHandled;
	int						mMinWidth;
	int						mMinHeight;
	bool					mMouseVisible;
	bool					mHasFocus;
	bool					mSoftHasFocus; // Mostly tracks mHasFocus except for when we get an explicit 'LostFocus' callback
	bool					mAwaitKeyReleases;
	int						mAwaitKeyReleasesCheckIdx;
	DWORD					mAwaitKeyReleasesEventTick;
	DWORD					mFocusLostTick;
	bool					mNeedsStateReset;
	bool					mKeyLayoutHasAltGr;

public:
	virtual LRESULT WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WindowProcStub(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	void					RehupMouseOver(bool isMouseOver);
	bool					CheckKeyReleases(bool isKeyDown);
	void					GotFocus();

public:
	WinBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags);
	~WinBFWindow();

	virtual void*			GetUnderlying() override;
	virtual void			Destroy() override;
	virtual bool			TryClose() override;
	virtual void			SetTitle(const char* title) override;
	virtual void			Show(ShowKind showKind) override;
	virtual void			SetForeground() override;
	virtual void			LostFocus(BFWindow* newFocus) override;
	virtual void			SetMinimumSize(int minWidth, int minHeight, bool clientSized) override;
	virtual void			GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight) override;
	virtual void			GetPlacement(int* normX, int* normY, int* normWidth, int* normHeight, int* showKind) override;
	virtual void			Resize(int x, int y, int width, int height, ShowKind showKind) override;
	virtual void			SetClientPosition(int x, int y) override;
	virtual void			SetMouseVisible(bool isMouseVisible) override;
	virtual void			SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible) override;
	virtual void			CaptureMouse() override;
	virtual bool			IsMouseCaptured() override;
	virtual int				GetDPI() override; // { return ::GetDpiForWindow(mHWnd); }

	virtual BFMenu*			AddMenuItem(BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) override;
	virtual void			ModifyMenuItem(BFMenu* item, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) override;
	virtual void			RemoveMenuItem(BFMenu* item) override;
	virtual void			ModalsRemoved() override;
};

class WinBFApp : public BFApp
{
public:
	bool					mInMsgProc;
	StringToUIntMap			mClipboardFormatMap;
	DSoundManager*			mDSoundManager;
	DInputManager*			mDInputManager;
	BfpThreadId				mVSyncThreadId;
	BfpThread*				mVSyncThread;
	volatile bool			mClosing;

protected:
	void					VSyncThreadProc();
	static void BFP_CALLTYPE VSyncThreadProcThunk(void* ptr);

	virtual void			Draw() override;
	virtual void			PhysSetCursor() override;

	uint32					GetClipboardFormat(const StringImpl& format);

public:
	WinBFApp();
	virtual ~WinBFApp();

	virtual void			Init() override;
	virtual void			Run() override;
	virtual void			Process() override;

	virtual void			GetDesktopResolution(int& width, int& height) override;
	virtual void			GetWorkspaceRect(int& x, int& y, int& width, int& height) override;
	virtual void			GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, int& outX, int& outY, int& outWidth, int& outHeight) override;
	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) override;
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window);

	virtual void*			GetClipboardData(const StringImpl& format, int* size) override;
	virtual void			ReleaseClipboardData(void* ptr) override;
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) override;
	virtual void			RehupMouse() override;

	virtual String			EnumerateInputDevices() override;
	virtual BFInputDevice*	CreateInputDevice(const StringImpl& guid) override;

	virtual BFSysBitmap*	LoadSysBitmap(const WCHAR* fileName) override;

	virtual BFSoundManager* GetSoundManager() override;

	virtual intptr			GetCriticalThreadId(int idx) override;
};

NS_BF_END;
