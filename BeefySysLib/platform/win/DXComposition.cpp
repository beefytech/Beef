#include "DXComposition.h"
#include "WinBFApp.h"
#include "BFApp.h"

#pragma comment(lib, "dcomp.lib")

USING_NS_BF;

static HRESULT gCompositionLastResult = S_OK;
// Frames in flight, counting the one being drawn, each with its own buffer. With one, any frame whose CPU plus GPU
// time runs past a refresh waits a whole extra refresh. Three keep a GPU-bound view's GPU a little busier, at the
// cost of another refresh of latency whenever we're ahead of the display.
static const int COMPOSITION_FRAME_LATENCY = 2;

///

DXCompositionHost::DXCompositionHost(DXRenderDevice* renderDevice, HWND hWnd)
{
	mRenderDevice = renderDevice;
	mHWnd = hWnd;
	mDCompDevice = NULL;
	mTarget = NULL;
	mRoot = NULL;
	mTreeDirty = false;
	mLastResult = S_OK;
	mRenderDevice->mCompositionHosts.Add(this);
}

DXCompositionHost::~DXCompositionHost()
{
	while (!mVisuals.IsEmpty())
		delete mVisuals.back();
	ReleaseNative();
	if (mRenderDevice != NULL)
		mRenderDevice->mCompositionHosts.Remove(this);
}

bool DXCompositionHost::InitNative()
{
	if ((mRenderDevice == NULL) || (mRenderDevice->mD3DDevice == NULL))
		return false;
	IDXGIDevice* dxgiDevice = NULL;
	mLastResult = mRenderDevice->mD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
	if (SUCCEEDED(mLastResult))
	{
		mLastResult = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**)&mDCompDevice);
		dxgiDevice->Release();
	}
	if (SUCCEEDED(mLastResult))
		mLastResult = mDCompDevice->CreateTargetForHwnd(mHWnd, TRUE, &mTarget);
	if (SUCCEEDED(mLastResult))
		mLastResult = mDCompDevice->CreateVisual(&mRoot);
	if (SUCCEEDED(mLastResult))
		mLastResult = mTarget->SetRoot(mRoot);
	if (FAILED(mLastResult))
	{
		ReleaseNative();
		return false;
	}
	mTreeDirty = true;
	return true;
}

void DXCompositionHost::ReleaseNative()
{
	if (mRoot != NULL)
		mRoot->Release();
	mRoot = NULL;
	if (mTarget != NULL)
		mTarget->Release();
	mTarget = NULL;
	if (mDCompDevice != NULL)
		mDCompDevice->Release();
	mDCompDevice = NULL;
}

void DXCompositionHost::ReinitNative()
{
	for (auto visual : mVisuals)
		visual->ReleaseNative();
	ReleaseNative();
	if (!InitNative())
		return;
	for (auto visual : mVisuals)
		visual->InitNative();
	Commit();
}

void DXCompositionHost::RebuildTree()
{
	mTreeDirty = false;
	if (mRoot == NULL)
		return;
	mRoot->RemoveAllVisuals();

	// Stable by mOrder, so equal orders stack in creation order.
	Array<DXCompositionVisual*> sorted;
	for (auto visual : mVisuals)
	{
		int insertIdx = sorted.mSize;
		while ((insertIdx > 0) && (sorted[insertIdx - 1]->mOrder > visual->mOrder))
			insertIdx--;
		sorted.Insert(insertIdx, visual);
	}

	IDCompositionVisual* below = NULL;
	for (auto visual : sorted)
	{
		if ((!visual->mVisible) || (visual->mVisual == NULL))
			continue;
		if (below == NULL)
			mRoot->AddVisual(visual->mVisual, FALSE, NULL);
		else
			mRoot->AddVisual(visual->mVisual, TRUE, below);
		below = visual->mVisual;
	}
}

HRESULT DXCompositionHost::Commit()
{
	if (mDCompDevice == NULL)
		return E_FAIL;
	if (mTreeDirty)
		RebuildTree();
	mLastResult = mDCompDevice->Commit();
	return mLastResult;
}

///

DXCompositionVisual::DXCompositionVisual(DXCompositionHost* host)
{
	mHost = host;
	mVisual = NULL;
	mSurfaceHandle = NULL;
	mExternalSurface = NULL;
	mSurface = NULL;
	mSurfaceWidth = 0;
	mSurfaceHeight = 0;
	mContentLost = false;
	mX = 0;
	mY = 0;
	mScaleX = 1;
	mScaleY = 1;
	mClipX = 0;
	mClipY = 0;
	mClipWidth = -1;
	mClipHeight = -1;
	mOrder = 0;
	mVisible = true;
	mHost->mVisuals.Add(this);
	mHost->mTreeDirty = true;
}

DXCompositionVisual::~DXCompositionVisual()
{
	if ((mHost->mRoot != NULL) && (mVisual != NULL))
		mHost->mRoot->RemoveVisual(mVisual);
	ReleaseNative();
	if (mSurfaceHandle != NULL)
		CloseHandle(mSurfaceHandle);
	mHost->mVisuals.Remove(this);
	mHost->mTreeDirty = true;
}

void DXCompositionVisual::ReleaseNative()
{
	if (mVisual != NULL)
		mVisual->Release();
	mVisual = NULL;
	if (mExternalSurface != NULL)
		mExternalSurface->Release();
	mExternalSurface = NULL;
	if (mSurface != NULL)
		mSurface->Release();
	mSurface = NULL;
}

HRESULT DXCompositionVisual::InitNative()
{
	auto dcomp = mHost->mDCompDevice;
	if (dcomp == NULL)
		return E_FAIL;
	HRESULT hr = dcomp->CreateVisual(&mVisual);
	if ((SUCCEEDED(hr)) && (mSurfaceHandle != NULL))
	{
		hr = dcomp->CreateSurfaceFromHandle(mSurfaceHandle, &mExternalSurface);
		if (SUCCEEDED(hr))
			hr = mVisual->SetContent(mExternalSurface);
	}
	if ((SUCCEEDED(hr)) && (mSurfaceWidth > 0) && (mSurfaceHeight > 0))
	{
		hr = dcomp->CreateSurface(mSurfaceWidth, mSurfaceHeight, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &mSurface);
		if (SUCCEEDED(hr))
			hr = mVisual->SetContent(mSurface);
		mContentLost = true;
	}
	if (SUCCEEDED(hr))
		ApplyProperties();
	mHost->mTreeDirty = true;
	return hr;
}

void DXCompositionVisual::ApplyProperties()
{
	if (mVisual == NULL)
		return;
	mVisual->SetOffsetX(mX);
	mVisual->SetOffsetY(mY);
	D2D_MATRIX_3X2_F transform = {};
	transform._11 = mScaleX;
	transform._22 = mScaleY;
	mVisual->SetTransform(transform);
	if (mClipWidth >= 0)
	{
		// In the visual's own space, so the scale applies to it too.
		IDCompositionRectangleClip* clip = NULL;
		if (SUCCEEDED(mHost->mDCompDevice->CreateRectangleClip(&clip)))
		{
			clip->SetLeft(mClipX);
			clip->SetTop(mClipY);
			clip->SetRight(mClipX + mClipWidth);
			clip->SetBottom(mClipY + mClipHeight);
			mVisual->SetClip(clip);
			clip->Release();
		}
	}
	else
		mVisual->SetClip((IDCompositionClip*)NULL);
}

HRESULT DXCompositionVisual::SetExternalSurface(HANDLE surfaceHandle)
{
	if (mExternalSurface != NULL)
		mExternalSurface->Release();
	mExternalSurface = NULL;
	if (mSurfaceHandle != NULL)
		CloseHandle(mSurfaceHandle);
	mSurfaceHandle = surfaceHandle;
	if (mVisual == NULL)
		return E_FAIL;
	if (mSurfaceHandle == NULL)
		return mVisual->SetContent(NULL);
	HRESULT hr = mHost->mDCompDevice->CreateSurfaceFromHandle(mSurfaceHandle, &mExternalSurface);
	if (SUCCEEDED(hr))
		hr = mVisual->SetContent(mExternalSurface);
	return hr;
}

HRESULT DXCompositionVisual::ResizeSurface(int width, int height)
{
	if (mSurface != NULL)
		mSurface->Release();
	mSurface = NULL;
	mSurfaceWidth = width;
	mSurfaceHeight = height;
	mContentLost = true;
	if ((mVisual == NULL) || (mHost->mDCompDevice == NULL))
		return E_FAIL;
	HRESULT hr = mHost->mDCompDevice->CreateSurface(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &mSurface);
	if (SUCCEEDED(hr))
		hr = mVisual->SetContent(mSurface);
	return hr;
}

// The texture must be single-sample RGBA8 holding premultiplied color, which is what Beefy2D
// render targets hold.
HRESULT DXCompositionVisual::UpdateFromTexture(Texture* texture, int srcX, int srcY, int width, int height)
{
	DXTexture* src = (DXTexture*)texture;
	if ((mSurface == NULL) || (src == NULL) || (src->mD3DTexture == NULL))
		return E_FAIL;
	width = BF_MIN(width, mSurfaceWidth);
	height = BF_MIN(height, mSurfaceHeight);
	if ((width <= 0) || (height <= 0))
		return S_OK;

	RECT updateRect = { 0, 0, width, height };
	ID3D11Texture2D* dest = NULL;
	POINT offset = {};
	HRESULT hr = mSurface->BeginDraw(&updateRect, __uuidof(ID3D11Texture2D), (void**)&dest, &offset);
	if (FAILED(hr))
		return hr;
	D3D11_BOX box = { (UINT)srcX, (UINT)srcY, 0, (UINT)(srcX + width), (UINT)(srcY + height), 1 };
	mHost->mRenderDevice->mD3DDeviceContext->CopySubresourceRegion(dest, 0, offset.x, offset.y, 0, src->mD3DTexture, 0, &box);
	dest->Release();
	hr = mSurface->EndDraw();
	if (SUCCEEDED(hr))
		mContentLost = false;
	return hr;
}

///

DXCompositionTarget::DXCompositionTarget(DXRenderDevice* renderDevice)
{
	mRenderDevice = renderDevice;
	mSurfaceHandle = NULL;
	mSwapChain = NULL;
	mWaitable = NULL;
	mPresentCount = 0;
	mAcquiredCount = 0;
	mTexture = NULL;
	mWidth = 0;
	mHeight = 0;
	mGeneration = 0;
	mTextureVersion = 0;
	mLastResult = S_OK;
	mRenderDevice->mCompositionTargets.Add(this);
}

DXCompositionTarget::~DXCompositionTarget()
{
	if (gBFApp->mFramePacer == this)
		gBFApp->mFramePacer = NULL;
	ReleaseSwapChain();
	if (mSurfaceHandle != NULL)
		CloseHandle(mSurfaceHandle);
	if (mRenderDevice != NULL)
		mRenderDevice->mCompositionTargets.Remove(this);
}

bool DXCompositionTarget::Create(int width, int height)
{
	mWidth = width;
	mHeight = height;
	mLastResult = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, NULL, &mSurfaceHandle);
	if (FAILED(mLastResult))
		return false;
	mGeneration++;
	mLastResult = CreateSwapChain();
	return SUCCEEDED(mLastResult);
}

HRESULT DXCompositionTarget::CreateSwapChain()
{
	IDXGIFactoryMedia* mediaFactory = NULL;
	HRESULT hr = mRenderDevice->mDXGIFactory->QueryInterface(__uuidof(IDXGIFactoryMedia), (void**)&mediaFactory);
	if (FAILED(hr))
		return hr;

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = mWidth;
	desc.Height = mHeight;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
	desc.BufferCount = COMPOSITION_FRAME_LATENCY;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	hr = mediaFactory->CreateSwapChainForCompositionSurfaceHandle(mRenderDevice->mD3DDevice, mSurfaceHandle, &desc, NULL, &mSwapChain);
	mediaFactory->Release();
	if (FAILED(hr))
		return hr;

	IDXGISwapChain2* swapChain2 = NULL;
	hr = mSwapChain->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&swapChain2);
	if (SUCCEEDED(hr))
	{
		swapChain2->SetMaximumFrameLatency(COMPOSITION_FRAME_LATENCY);
		mWaitable = swapChain2->GetFrameLatencyWaitableObject();
		mPresentCount = 0;
		mAcquiredCount = 0;
		swapChain2->Release();
	}
	if (FAILED(hr))
		return hr;
	return WrapBackBuffer();
}

void DXCompositionTarget::ReleaseSwapChain()
{
	DetachTexture();
	if (mWaitable != NULL)
		CloseHandle(mWaitable);
	mWaitable = NULL;
	if (mSwapChain != NULL)
		mSwapChain->Release();
	mSwapChain = NULL;
}

HRESULT DXCompositionTarget::WrapBackBuffer()
{
	auto device = mRenderDevice->mD3DDevice;
	ID3D11Texture2D* backBuffer = NULL;
	HRESULT hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
	if (FAILED(hr))
		return hr;
	ID3D11RenderTargetView* rtv = NULL;
	ID3D11ShaderResourceView* srv = NULL;
	hr = device->CreateRenderTargetView(backBuffer, NULL, &rtv);
	if (SUCCEEDED(hr))
		hr = device->CreateShaderResourceView(backBuffer, NULL, &srv);
	if (FAILED(hr))
	{
		if (rtv != NULL)
			rtv->Release();
		backBuffer->Release();
		return hr;
	}

	mTexture = new DXTexture();
	mTexture->mWidth = mWidth;
	mTexture->mHeight = mHeight;
	mTexture->mRenderDevice = mRenderDevice;
	mTexture->mD3DTexture = backBuffer;
	mTexture->mD3DRenderTargetView = rtv;
	mTexture->mD3DResourceView = srv;
	mTexture->mD3DFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	mTexture->mSampleCount = 1;
	mRenderDevice->mAllTextures.Add(mTexture);
	mTexture->AddRef();
	mTextureVersion++;
	return S_OK;
}

// Drops every reference we hold on the back buffer (ResizeBuffers refuses while one is alive). The
// DXTexture itself lives on as an empty shell while wrappers handed out by GetTexture still hold it.
void DXCompositionTarget::DetachTexture()
{
	if (mTexture == NULL)
		return;
	if ((mRenderDevice != NULL) && (mRenderDevice->mD3DDeviceContext != NULL))
	{
		auto ctx = mRenderDevice->mD3DDeviceContext;
		for (int i = 0; i < 32; i++)
		{
			if (mRenderDevice->mPSBoundTextures[i] != mTexture)
				continue;
			ID3D11ShaderResourceView* nullSrv = NULL;
			ctx->PSSetShaderResources(i, 1, &nullSrv);
			if (i >= DX_VS_TEXTURE_SLOT)
				ctx->VSSetShaderResources(i, 1, &nullSrv);
			mRenderDevice->mPSBoundTextures[i] = NULL;
		}
		if ((mRenderDevice->mCurTargetTexture == mTexture) || (mRenderDevice->mCurD3DRTV == mTexture->mD3DRenderTargetView))
		{
			ctx->OMSetRenderTargets(0, NULL, NULL);
			mRenderDevice->mCurTargetTexture = NULL;
			mRenderDevice->mCurD3DRTV = NULL;
			mRenderDevice->mCurD3DDSV = NULL;
		}
		if (mRenderDevice->mCurRenderTarget == mTexture)
			mRenderDevice->mCurRenderTarget = NULL;
	}
	mTexture->ReleaseNative();
	mTexture->mWidth = 0;
	mTexture->mHeight = 0;
	mTexture->Release();
	mTexture = NULL;
	if ((mRenderDevice != NULL) && (mRenderDevice->mD3DDeviceContext != NULL))
		mRenderDevice->mD3DDeviceContext->Flush();
}

HRESULT DXCompositionTarget::Resize(int width, int height)
{
	if (mSwapChain == NULL)
		return E_FAIL;
	DetachTexture();
	mLastResult = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
	if (SUCCEEDED(mLastResult))
	{
		mWidth = width;
		mHeight = height;
	}
	else if ((mLastResult == DXGI_ERROR_DEVICE_REMOVED) || (mLastResult == DXGI_ERROR_DEVICE_RESET))
		mRenderDevice->mNeedsReinitNative = true;
	// Even a failed resize leaves the old buffers usable again.
	HRESULT wrapResult = WrapBackBuffer();
	return FAILED(mLastResult) ? mLastResult : wrapResult;
}

HRESULT DXCompositionTarget::Present(int syncInterval)
{
	if (mSwapChain == NULL)
		return E_FAIL;
	// Present(0) was seen to lose frame-latency releases, leaving WaitForFrame waiting for good, so
	// unthrottled callers skip the frames the display has no room for yet instead.
	if ((syncInterval == 0) && (!WaitForFrame(0)))
		return S_FALSE;
	// The semaphore is capped (at 40) and releases past the cap are lost, so take them as they come.
	TakeReleases();
	mLastResult = mSwapChain->Present(1, 0);
	if (SUCCEEDED(mLastResult))
		mPresentCount++;
	if ((mLastResult == DXGI_ERROR_DEVICE_REMOVED) || (mLastResult == DXGI_ERROR_DEVICE_RESET))
		mRenderDevice->mNeedsReinitNative = true;
	return mLastResult;
}

// After the device was re-created: a fresh swapchain on the same surface when it takes one, else a
// new surface (and a new generation for the host to pick up).
void DXCompositionTarget::ReinitNative()
{
	ReleaseSwapChain();
	mLastResult = CreateSwapChain();
	if (FAILED(mLastResult))
	{
		ReleaseSwapChain();
		if (mSurfaceHandle != NULL)
			CloseHandle(mSurfaceHandle);
		mSurfaceHandle = NULL;
		Create(mWidth, mHeight);
	}
}

void DXCompositionTarget::TakeReleases()
{
	if (mWaitable == NULL)
		return;
	while (::WaitForSingleObjectEx(mWaitable, 0, TRUE) == WAIT_OBJECT_0)
		mAcquiredCount++;
}

// The frame-latency waitable is a semaphore that starts at the latency and is released once for
// every present, even across a resize. Taking one count per frame isn't enough: a present made
// without a wait (before pacing began, after a timed-out wait) leaves a spare count, and once a few
// pile up the wait stops blocking and Present blocks instead, spinning the CPU. So a frame is due
// only once we've taken a count for every present so far, and none is ever written off.
bool DXCompositionTarget::WaitForFrame(int timeoutMS)
{
	if (mWaitable == NULL)
		return false;
	uint32 startTick = BFTickCount();
	while (true)
	{
		TakeReleases();
		if (mAcquiredCount > mPresentCount)
			return true;
		int remaining = timeoutMS - (int)(BFTickCount() - startTick);
		if ((remaining <= 0) || (::WaitForSingleObjectEx(mWaitable, (DWORD)remaining, TRUE) != WAIT_OBJECT_0))
			return false;
		mAcquiredCount++;
	}
}

///

BF_EXPORT int BF_CALLTYPE Gfx_Composition_GetLastResult()
{
	return (int)gCompositionLastResult;
}

BF_EXPORT DXCompositionHost* BF_CALLTYPE Gfx_CompositionHost_Create(BFWindow* window)
{
	auto renderDevice = (DXRenderDevice*)gBFApp->mRenderDevice;
	auto host = new DXCompositionHost(renderDevice, ((WinBFWindow*)window)->mHWnd);
	if (!host->InitNative())
	{
		gCompositionLastResult = host->mLastResult;
		delete host;
		return NULL;
	}
	return host;
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionHost_Delete(DXCompositionHost* host)
{
	delete host;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionHost_Commit(DXCompositionHost* host)
{
	return (int)host->Commit();
}

// Takes ownership of surfaceHandle, which must already be valid in this process.
BF_EXPORT DXCompositionVisual* BF_CALLTYPE Gfx_CompositionVisual_CreateExternal(DXCompositionHost* host, void* surfaceHandle)
{
	auto visual = new DXCompositionVisual(host);
	visual->mSurfaceHandle = (HANDLE)surfaceHandle;
	HRESULT hr = visual->InitNative();
	if (FAILED(hr))
	{
		gCompositionLastResult = hr;
		delete visual;
		return NULL;
	}
	return visual;
}

BF_EXPORT DXCompositionVisual* BF_CALLTYPE Gfx_CompositionVisual_CreateSurface(DXCompositionHost* host, int width, int height)
{
	auto visual = new DXCompositionVisual(host);
	visual->mSurfaceWidth = width;
	visual->mSurfaceHeight = height;
	HRESULT hr = visual->InitNative();
	if (FAILED(hr))
	{
		gCompositionLastResult = hr;
		delete visual;
		return NULL;
	}
	return visual;
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_Delete(DXCompositionVisual* visual)
{
	delete visual;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionVisual_SetExternalSurface(DXCompositionVisual* visual, void* surfaceHandle)
{
	return (int)visual->SetExternalSurface((HANDLE)surfaceHandle);
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionVisual_ResizeSurface(DXCompositionVisual* visual, int width, int height)
{
	return (int)visual->ResizeSurface(width, height);
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionVisual_UpdateFromTexture(DXCompositionVisual* visual, TextureSegment* textureSegment, int srcX, int srcY, int width, int height)
{
	return (int)visual->UpdateFromTexture(textureSegment->mTexture, srcX, srcY, width, height);
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_SetOffset(DXCompositionVisual* visual, float x, float y)
{
	visual->mX = x;
	visual->mY = y;
	visual->ApplyProperties();
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_SetScale(DXCompositionVisual* visual, float scaleX, float scaleY)
{
	visual->mScaleX = scaleX;
	visual->mScaleY = scaleY;
	visual->ApplyProperties();
}

// A negative width removes the clip.
BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_SetClip(DXCompositionVisual* visual, float x, float y, float width, float height)
{
	visual->mClipX = x;
	visual->mClipY = y;
	visual->mClipWidth = width;
	visual->mClipHeight = height;
	visual->ApplyProperties();
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_SetVisible(DXCompositionVisual* visual, int visible)
{
	if (visual->mVisible == (visible != 0))
		return;
	visual->mVisible = visible != 0;
	visual->mHost->mTreeDirty = true;
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionVisual_SetOrder(DXCompositionVisual* visual, int order)
{
	if (visual->mOrder == order)
		return;
	visual->mOrder = order;
	visual->mHost->mTreeDirty = true;
}

// True once after the surface's pixels were lost (created, resized or device re-created).
BF_EXPORT int BF_CALLTYPE Gfx_CompositionVisual_TakeContentLost(DXCompositionVisual* visual)
{
	bool lost = visual->mContentLost;
	visual->mContentLost = false;
	return lost ? 1 : 0;
}

// Clears a render target to a premultiplied 0xAARRGGBB color right away, ahead of anything queued to draw into it.
// Image.Clear only schedules a debug-color clear.
BF_EXPORT void BF_CALLTYPE Gfx_Texture_ClearToColor(TextureSegment* textureSegment, uint32 color)
{
	DXTexture* texture = (DXTexture*)textureSegment->mTexture;
	if ((texture == NULL) || (texture->mD3DRenderTargetView == NULL) || (texture->mRenderDevice == NULL))
		return;
	float rgba[4] = { ((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f };
	texture->mRenderDevice->mD3DDeviceContext->ClearRenderTargetView(texture->mD3DRenderTargetView, rgba);
}

BF_EXPORT DXCompositionTarget* BF_CALLTYPE Gfx_CompositionTarget_Create(int width, int height)
{
	auto target = new DXCompositionTarget((DXRenderDevice*)gBFApp->mRenderDevice);
	if (!target->Create(width, height))
	{
		gCompositionLastResult = target->mLastResult;
		delete target;
		return NULL;
	}
	return target;
}

BF_EXPORT void BF_CALLTYPE Gfx_CompositionTarget_Delete(DXCompositionTarget* target)
{
	delete target;
}

// A new wrapper around the current back buffer, owned by the caller. Resize and device re-creation
// leave earlier wrappers empty; take a new one after either.
BF_EXPORT TextureSegment* BF_CALLTYPE Gfx_CompositionTarget_GetTexture(DXCompositionTarget* target)
{
	if (target->mTexture == NULL)
		return NULL;
	target->mTexture->AddRef();
	TextureSegment* segment = new TextureSegment();
	segment->InitFromTexture(target->mTexture);
	return segment;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_Resize(DXCompositionTarget* target, int width, int height)
{
	return (int)target->Resize(width, height);
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_Present(DXCompositionTarget* target, int syncInterval)
{
	return (int)target->Present(syncInterval);
}

BF_EXPORT FramePacer* BF_CALLTYPE Gfx_CompositionTarget_GetFramePacer(DXCompositionTarget* target)
{
	return target;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_WaitForFrame(DXCompositionTarget* target, int timeoutMS)
{
	return target->WaitForFrame(timeoutMS) ? 1 : 0;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_GetGeneration(DXCompositionTarget* target)
{
	return target->mGeneration;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_GetTextureVersion(DXCompositionTarget* target)
{
	return target->mTextureVersion;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_GetWidth(DXCompositionTarget* target)
{
	return target->mWidth;
}

BF_EXPORT int BF_CALLTYPE Gfx_CompositionTarget_GetHeight(DXCompositionTarget* target)
{
	return target->mHeight;
}

// Duplicates the surface handle into another process; returns the value valid there, or 0.
BF_EXPORT uint64 BF_CALLTYPE Gfx_CompositionTarget_DuplicateHandleTo(DXCompositionTarget* target, int processId)
{
	HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)processId);
	if (process == NULL)
	{
		gCompositionLastResult = HRESULT_FROM_WIN32(GetLastError());
		return 0;
	}
	HANDLE remote = NULL;
	if (!DuplicateHandle(GetCurrentProcess(), target->mSurfaceHandle, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		gCompositionLastResult = HRESULT_FROM_WIN32(GetLastError());
		remote = NULL;
	}
	CloseHandle(process);
	return (uint64)remote;
}
