#pragma once

#include "Common.h"
#include "util/CritSect.h"
#include <list>

NS_BF_BEGIN;

typedef void (*BFApp_UpdateFunc)(bool batchStart);
typedef void (*BFApp_UpdateFFunc)(float updatePct);
typedef void (*BFApp_DrawFunc)(bool forceDraw);

class BFApp;
class BFSoundManager;
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

class BFInputDevice
{
public:
	virtual ~BFInputDevice() {}
	virtual String GetState() = 0;
};

class BFApp
{
public:
	CritSect				mCritSect;
	String					mTitle;
	String					mInstallDir;
	String					mDataDir;
	bool					mDrawEnabled;
	float					mRefreshRate;	
	int						mMaxUpdatesPerDraw;	
	double					mUpdateCntF;
	double					mClientUpdateCntF;

	bool					mInProcess;
	bool					mRunning;
	RenderDevice*			mRenderDevice;
	int						mSysDialogCnt;
	int						mUpdateCnt;
	int						mNumPhysUpdates;
	SyncEvent				mVSyncEvent;
	volatile bool			mVSyncActive;
	bool                    mVSynched;
	bool					mVSyncFailed;
	bool					mForceNextDraw;

	int                     mUpdateSampleCount;
	int                     mUpdateSampleTimes;

	uint32 mLastProcessTick;
	float mPhysFrameTimeAcc;

	BFApp_UpdateFunc		mUpdateFunc;
	BFApp_UpdateFFunc		mUpdateFFunc;
	BFApp_DrawFunc			mDrawFunc;
	int						mCursor;

	BFWindowList			mWindowList;
	BFWindowList			mPendingWindowDeleteList;

public:
	virtual void			Update(bool batchStart);
	virtual void			UpdateF(float updatePct);
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
	virtual void			GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, int& outX, int& outY, int& outWidth, int& outHeight) { GetWorkspaceRect(outX, outY, outWidth, outHeight); }

	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) = 0;
	virtual void			RemoveWindow(BFWindow* window);
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window) = 0;

	virtual void*			GetClipboardData(const StringImpl& format, int* size) = 0;
	virtual void			ReleaseClipboardData(void* ptr) = 0;
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) = 0;
	virtual void			RehupMouse() {}

	virtual String			EnumerateInputDevices() { return ""; }
	virtual BFInputDevice*	CreateInputDevice(const StringImpl& guid) { return NULL; }

	virtual BFSysBitmap*	LoadSysBitmap(const wchar_t* fileName) = 0;

	virtual FileStream*		OpenBinaryFile(const StringImpl& fileName);

	virtual BFSoundManager* GetSoundManager() { return NULL; }

	virtual intptr			GetCriticalThreadId(int idx) { return 0; }
};

extern BFApp* gBFApp;

NS_BF_END;