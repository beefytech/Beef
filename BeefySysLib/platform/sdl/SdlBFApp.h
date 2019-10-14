#pragma once

#include "BFApp.h"
#include "BFWindow.h"
#include <map>

struct SDL_Window;

NS_BF_BEGIN;

class RenderDevice;

typedef std::map<String, uint32> StringToUIntMap;

class SdlBFWindow : public BFWindow
{
public:
	SDL_Window*				mSDLWindow;
	bool					mIsMouseInside;	
	int						mModalCount;	

public:
	SdlBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags);
	~SdlBFWindow();

	virtual bool			TryClose() override;
	virtual void			GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight) override;
	virtual void			SetClientPosition(int x, int y) override;
	virtual void			SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible) override;
    virtual BFMenu*         AddMenuItem(BFMenu* parent, const wchar_t* text, const wchar_t* hotKey, BFSysBitmap* sysBitmap, bool enabled, int checkState, bool radioCheck);

	virtual void			RemoveMenuItem(BFMenu* item) override;
	virtual void			ModalsRemoved() override;
};

typedef std::map<uint32, SdlBFWindow*> SdlWindowMap;

class SdlBFApp : public BFApp
{
public:
	bool					mInMsgProc;
	StringToUIntMap			mClipboardFormatMap;
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