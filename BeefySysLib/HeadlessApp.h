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

	virtual void			GetDesktopResolution(int& width, int& height) override { }
	virtual void			GetWorkspaceRect(int& x, int& y, int& width, int& height) override {}

	virtual BFWindow*		CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags) override { return NULL; }
	virtual DrawLayer*		CreateDrawLayer(BFWindow* window) override { return NULL; }

	virtual void*			GetClipboardData(const StringImpl& format, int* size) override { return NULL; }
	virtual void			ReleaseClipboardData(void* ptr) override { }
	virtual void			SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard) override { }

	virtual BFSysBitmap*	LoadSysBitmap(const wchar_t* fileName) override { return NULL; }
	
};

NS_BF_END;
