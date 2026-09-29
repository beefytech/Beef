#pragma once

#include "DXRenderDevice.h"
#include "BFApp.h"
#include <dxgi1_3.h>
#include <dcomp.h>

NS_BF_BEGIN;

class BFWindow;
class DXCompositionVisual;

// Shows other processes' swapchains (and our own overlay surfaces) inside one of our windows through
// DirectComposition. Composed content always sits above whatever the window draws or presents itself.
class DXCompositionHost
{
public:
	DXRenderDevice*			mRenderDevice;
	HWND					mHWnd;
	IDCompositionDevice*	mDCompDevice;
	IDCompositionTarget*	mTarget;
	IDCompositionVisual*	mRoot;
	// Bottom to top.
	Array<DXCompositionVisual*> mVisuals;
	bool					mTreeDirty;
	HRESULT					mLastResult;

public:
	DXCompositionHost(DXRenderDevice* renderDevice, HWND hWnd);
	~DXCompositionHost();

	bool					InitNative();
	void					ReleaseNative();
	// Device re-creation: rebuilds the DComp device and every visual. Surface visuals come back
	// empty with mContentLost set.
	void					ReinitNative();
	void					RebuildTree();
	HRESULT					Commit();
};

class DXCompositionVisual
{
public:
	DXCompositionHost*		mHost;
	IDCompositionVisual*	mVisual;
	// External content: a composition surface handle (owned, valid in this process) that another
	// process presents into.
	HANDLE					mSurfaceHandle;
	IUnknown*				mExternalSurface;
	// Our own content: a premultiplied RGBA8 surface filled from a texture.
	IDCompositionSurface*	mSurface;
	int						mSurfaceWidth;
	int						mSurfaceHeight;
	bool					mContentLost;
	float					mX;
	float					mY;
	float					mScaleX;
	float					mScaleY;
	// In the visual's own space, before its scale.
	float					mClipX;
	float					mClipY;
	float					mClipWidth; // < 0 = unclipped
	float					mClipHeight;
	int						mOrder;
	bool					mVisible;

public:
	DXCompositionVisual(DXCompositionHost* host);
	~DXCompositionVisual();

	void					ReleaseNative();
	HRESULT					InitNative();
	void					ApplyProperties();
	HRESULT					SetExternalSurface(HANDLE surfaceHandle);
	HRESULT					ResizeSurface(int width, int height);
	HRESULT					UpdateFromTexture(Texture* texture, int srcX, int srcY, int width, int height);
};

// A swapchain presented into a composition surface that another process (the IDE) shows. The engine
// draws into GetTexture() like any render target and calls Present itself.
class DXCompositionTarget : public FramePacer
{
public:
	DXRenderDevice*			mRenderDevice;
	HANDLE					mSurfaceHandle;
	IDXGISwapChain1*		mSwapChain;
	HANDLE					mWaitable;
	// Since the swapchain was created: presents made, and frame-latency semaphore counts taken.
	int64					mPresentCount;
	int64					mAcquiredCount;
	// Wraps back buffer 0 (flip model keeps the current back buffer at index 0).
	DXTexture*				mTexture;
	int						mWidth;
	int						mHeight;
	// Bumped whenever mSurfaceHandle is replaced, so the host knows to re-attach.
	int						mGeneration;
	// Bumped whenever mTexture is replaced (resize, device re-creation): earlier wrappers are empty.
	int						mTextureVersion;
	HRESULT					mLastResult;

public:
	DXCompositionTarget(DXRenderDevice* renderDevice);
	~DXCompositionTarget();

	bool					Create(int width, int height);
	HRESULT					CreateSwapChain();
	void					ReleaseSwapChain();
	HRESULT					WrapBackBuffer();
	void					DetachTexture();
	HRESULT					Resize(int width, int height);
	HRESULT					Present(int syncInterval);
	void					ReinitNative();
	void					TakeReleases();

	virtual bool			WaitForFrame(int timeoutMS) override;
};

NS_BF_END;
