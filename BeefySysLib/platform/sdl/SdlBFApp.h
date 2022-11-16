#pragma once

#include "BFApp.h"
#include "BFWindow.h"
#include <map>
#include "util/Dictionary.h"

struct SDL_Window;

NS_BF_BEGIN;

class RenderDevice;

class SdlBFWindow : public BFWindow
{
public:
	SDL_Window*				mSDLWindow;
	bool					mIsMouseInside;
	int						mModalCount;

public:
	SdlBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags);
	~SdlBFWindow();

	virtual void*			GetUnderlying() {return mSDLWindow; };
	virtual void			Destroy() { }

	virtual void			SetTitle(const char* title) override {}
	virtual void			SetMinimumSize(int minWidth, int minHeight, bool clientSized) override {}
	virtual void			GetPlacement(int* normX, int* normY, int* normWidth, int* normHeight, int* showKind) override { }
	virtual void			Resize(int x, int y, int width, int height, ShowKind showKind) override {}
	virtual void			SetMouseVisible(bool isMouseVisible) override {}

	virtual bool			TryClose() override;
	virtual void			GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight) override;
	virtual void			SetClientPosition(int x, int y) override;
	virtual void			SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible) override;
	virtual BFMenu*			AddMenuItem(BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) override;
	virtual void			ModifyMenuItem(BFMenu* item, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck) override {}
	virtual void			RemoveMenuItem(BFMenu* item) override;

	virtual void			LostFocus(BFWindow* newFocus) override {};

	virtual void			ModalsRemoved() override;

	virtual void			Show(ShowKind showKind) {}
	virtual void			SetForeground() override {};
};

typedef Dictionary<uint32, SdlBFWindow*> SdlWindowMap;

class SdlBFApp : public BFApp
{
public:
	bool					mInMsgProc;
	SdlWindowMap			mSdlWindowMap;

protected:
	virtual void			Draw() override;
	virtual void			PhysSetCursor() override;

	uint32					GetClipboardFormat(const StringImpl& format);
	SdlBFWindow*			GetSdlWindowFromId(uint32 id);

public:
	SdlBFApp();
	virtual ~SdlBFApp();

	virtual void			Init() override;
	virtual void			Run() override;

	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) override;
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window) override;

	virtual void*			GetClipboardData(const StringImpl& format, int* size) override;
	virtual void			ReleaseClipboardData(void* ptr) override;
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) override;

	virtual BFSysBitmap*	LoadSysBitmap(const wchar_t* fileName) override;
	virtual void            GetDesktopResolution(int& width, int& height) override;
	virtual void            GetWorkspaceRect(int& x, int& y, int& width, int& height) override;
};

NS_BF_END;