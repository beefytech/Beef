#pragma once

#include "Common.h"
#include <list>

NS_BF_BEGIN;

typedef void (*BFApp_UpdateFunc)(bool batchStart);
typedef void (*BFApp_DrawFunc)();

class BFApp;
class RenderDevice;
class BFWindow;
class FileStream;
class DrawLayer;

typedef std::list<BFWindow*> BFWindowList;

enum
{
	CURSOR_POINTER,
	CURSOR_HAND,
	CURSOR_DRAGGING,
	CURSOR_TEXT,
	CURSOR_CIRCLE_SLASH,
	CURSOR_SIZEALL,
	CURSOR_SIZENESW,
	CURSOR_SIZENS,
	CURSOR_SIZENWSE,
	CURSOR_SIZEWE,	
	CURSOR_WAIT,
	CURSOR_NONE,	

	NUM_CURSORS
};

class BFSysBitmap
{
public:
};

class BFApp
{
public:
	String					mTitle;
	String					mInstallDir;
	String					mDataDir;
	bool					mDrawEnabled;
	float					mRefreshRate;
	int						mMaxUpdatesPerDraw;

	bool					mInProcess;
	bool					mRunning;
	RenderDevice*			mRenderDevice;
	int						mSysDialogCnt;
	int						mUpdateCnt;
	bool                    mVSynched;
	bool					mVSyncFailed;

	int                     mUpdateSampleCount;
	int                     mUpdateSampleTimes;

	uint32 mLastProcessTick;
	float mFrameTimeAcc;

	BFApp_UpdateFunc		mUpdateFunc;
	BFApp_DrawFunc			mDrawFunc;
	int						mCursor;

	BFWindowList			mWindowList;
	BFWindowList			mPendingWindowDeleteList;

public:
	virtual void			Update(bool batchStart);
	virtual void			Draw();
	virtual void			Process();
	virtual void			PhysSetCursor() = 0;

public:
	BFApp();
	virtual ~BFApp();

	virtual void			Init();
	virtual void			Run();
	virtual void			Shutdown();
	virtual void			SetCursor(int cursor);
	virtual void			GetDesktopResolution(int& width, int& height) = 0;
	virtual void			GetWorkspaceRect(int& x, int& y, int& width, int& height) = 0;

	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) = 0;
	virtual void			RemoveWindow(BFWindow* window);
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window) = 0;

	virtual void*			GetClipboardData(const StringImpl& format, int* size) = 0;
	virtual void			ReleaseClipboardData(void* ptr) = 0;
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) = 0;
	virtual void			RehupMouse() {}

	virtual BFSysBitmap*	LoadSysBitmap(const wchar_t* fileName) = 0;

	virtual FileStream*		OpenBinaryFile(const StringImpl& fileName);
};

extern BFApp* gBFApp;

NS_BF_END;