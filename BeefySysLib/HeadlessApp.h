#pragma once

#include "BFApp.h"
#include "BFWindow.h"

NS_BF_BEGIN;

class HeadlessApp : public BFApp
{
public:
    virtual void			Init() override;
    virtual void			Run() override;

	virtual void			PhysSetCursor() override { }

	virtual void			GetDesktopResolution(int& width, int& height) { }
	virtual void			GetWorkspaceRect(int& x, int& y, int& width, int& height) {}

	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) { return NULL; }
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window) { return NULL; }

	virtual void*			GetClipboardData(const StringImpl& format, int* size) { return NULL; }
	virtual void			ReleaseClipboardData(void* ptr) { }
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) { }

	virtual BFSysBitmap*	LoadSysBitmap(const wchar_t* fileName) { return NULL; }
	
};

NS_BF_END;
