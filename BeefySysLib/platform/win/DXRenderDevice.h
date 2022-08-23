#pragma once

#ifdef BF_MINGW
#define	D3D11_APPEND_ALIGNED_ELEMENT	( 0xffffffff )
#ifndef __C89_NAMELESS
#define __C89_NAMELESS
#define __C89_NAMELESSUNIONNAME
#endif
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-attributes"
#pragma clang diagnostic ignored "-Wunused-member-function"
#pragma clang diagnostic ignored "-Wunused-conversion-function"
#define __in
#define __in_opt
#define __in_ecount(a)
#define __in_ecount_opt(a)
#define __in_bcount(a)
#define __in_bcount_opt(a)
#define __inout
#define __inout_opt
#define __out
#define __out_opt
#define __out_bcount(a)
#define __out_bcount_opt(a)
#define __out_ecount(a)
#define __out_ecount_opt(a)
#define __out_ecount_part_opt(a, b)
#endif

#pragma warning (push)
#pragma warning (disable:4005)
#include <d3d11.h>
#pragma warning (pop)

#ifdef BF_MINGW
#undef __in
#undef __out
#endif

#include "Common.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "gfx/RenderDevice.h"
#include "gfx/DrawLayer.h"
#include "gfx/ModelInstance.h"
#include "util/HashSet.h"
#include "util/Dictionary.h"
#include <map>

NS_BF_BEGIN;

class WinBFWindow;
class BFApp;
class DXRenderDevice;

class DXTexture : public Texture
{
public:
	String					mPath;
	DXRenderDevice*			mRenderDevice;
	ID3D11Texture2D*		mD3DTexture;
	ID3D11ShaderResourceView* mD3DResourceView;
	ID3D11RenderTargetView*	mD3DRenderTargetView;
	ID3D11Texture2D*		mD3DDepthBuffer;
	ID3D11DepthStencilView*	mD3DDepthStencilView;
	uint32*					mContentBits;

public:
	DXTexture();
	~DXTexture();

	void					ReleaseNative();
	void					ReinitNative();

	virtual void			PhysSetAsTarget() override;
	virtual void			Blt(ImageData* imageData, int x, int y) override;
	virtual void			SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits) override;
	virtual void			GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) override;
};

class DXShaderParam : public ShaderParam
{
public:
	ID3D10EffectVariable*	mD3DVariable;

public:
	DXShaderParam();
	~DXShaderParam();

	virtual void			SetTexture(Texture* texture);
	virtual void			SetFloat4(float x, float y, float z, float w) override;
};

typedef std::map<String, DXShaderParam*> DXShaderParamMap;

class DXShader : public Shader
{
public:
	DXRenderDevice*			mRenderDevice;
	String					mSrcPath;
	VertexDefinition*		mVertexDef;

	ID3D11InputLayout*		mD3DLayout;
	ID3D11VertexShader*		mD3DVertexShader;
	ID3D11PixelShader*		mD3DPixelShader;
	DXShaderParamMap		mParamsMap;
	ID3D11Buffer*			mConstBuffer;
	bool					mHas2DPosition;

public:
	DXShader();
	~DXShader();

	void					ReleaseNative();
	void					ReinitNative();

	bool					Load();
	virtual ShaderParam*	GetShaderParam(const StringImpl& name) override;
};

class DXDrawBatch : public DrawBatch
{
public:

public:
	DXDrawBatch();
	~DXDrawBatch();

	virtual void			Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXDrawLayer : public DrawLayer
{
public:
	virtual DrawBatch*		CreateDrawBatch();
	virtual RenderCmd*		CreateSetTextureCmd(int textureIdx, Texture* texture) override;
	virtual void			SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size) override;
	virtual void			SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount) override;

public:
	DXDrawLayer();
	~DXDrawLayer();
};

class DXRenderWindow : public RenderWindow
{
public:
	HWND					mHWnd;
	DXRenderDevice*			mDXRenderDevice;
	IDXGISwapChain*			mDXSwapChain;
	ID3D11Texture2D*		mD3DBackBuffer;
	ID3D11RenderTargetView*	mD3DRenderTargetView;
	ID3D11Texture2D*		mD3DDepthBuffer;
	ID3D11DepthStencilView*	mD3DDepthStencilView;
	HANDLE					mFrameWaitObject;
	float					mRefreshRate;
	bool					mResizePending;
	bool					mWindowed;
	int						mPendingWidth;
	int						mPendingHeight;

public:
	virtual void			PhysSetAsTarget();
	void					CheckDXResult(HRESULT result);

public:
	DXRenderWindow(DXRenderDevice* renderDevice, WinBFWindow* window, bool windowed);
	~DXRenderWindow();

	void ReleaseNative();
	void ReinitNative();

	void					SetAsTarget() override;
	void					Resized() override;
	virtual void			Present() override;

	void					CopyBitsTo(uint32* dest, int width, int height);
	virtual float			GetRefreshRate() override;
	virtual bool			WaitForVBlank() override;
};

typedef std::vector<DXDrawBatch*> DXDrawBatchVector;

#define DX_VTXBUFFER_SIZE 1024*1024
#define DX_IDXBUFFER_SIZE 64*1024

class DXDrawBufferPool
{
public:
	std::vector<void*>		mPooledIndexBuffers;
	int						mIdxPoolIdx;
	std::vector<void*>		mPooledVertexBuffers;
	int						mVtxPoolIdx;

	void*					mIndexBuffer;
	void*					mVertexBuffer;
	int						mIdxByteIdx;
	int						mVtxByteIdx;

	void					AllocateIndices(int minIndices);
	void					AllocVertices(int minVertices);
};

class DXRenderState : public RenderState
{
public:
	ID3D11RasterizerState*	mD3DRasterizerState;
	ID3D11DepthStencilState* mD3DDepthStencilState;

public:
	DXRenderState();
	~DXRenderState();

	void ReleaseNative();
	void ReinitNative();

	void InvalidateRasterizerState();
	void IndalidateDepthStencilState();

	virtual void SetClipped(bool clipped);
	virtual void SetTexWrap(bool clipped);
	virtual void SetClipRect(const Rect& rect);
	virtual void SetWriteDepthBuffer(bool writeDepthBuffer);
	virtual void SetDepthFunc(DepthFunc depthFunc);
};

class DXModelPrimitives
{
public:
	String					mMaterialName;
	int						mNumIndices;
	int						mNumVertices;
	Array<DXTexture*>		mTextures;

	ID3D11Buffer*			mD3DIndexBuffer;
	//TODO: Split the vertex buffer up into static and dynamic buffers
	ID3D11Buffer*			mD3DVertexBuffer;

public:
	DXModelPrimitives();
	~DXModelPrimitives();
};

class DXModelMesh
{
public:
	Array<DXModelPrimitives> mPrimitives;
};

class DXModelInstance : public ModelInstance
{
public:
	DXRenderDevice*			mD3DRenderDevice;
	Array<DXModelMesh>		mDXModelMeshs;

public:
	DXModelInstance(ModelDef* modelDef);
	~DXModelInstance();

	virtual void CommandQueued(DrawLayer* drawLayer) override;
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXVertexDefinition : public VertexDefinition
{
public:
	~DXVertexDefinition();
};

class DXSetTextureCmd : public RenderCmd
{
public:
	int						mTextureIdx;
	Texture*				mTexture;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXSetConstantData : public RenderCmd
{
public:
	int mUsageIdx; // 0 = VS, 1 = PS
	int mSlotIdx;
	int mSize;
	uint8 mData[1];

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXRenderDevice : public RenderDevice
{
public:
	IDXGIFactory*			mDXGIFactory;
	ID3D11Device*			mD3DDevice;
	ID3D11DeviceContext*	mD3DDeviceContext;
	ID3D11BlendState*		mD3DNormalBlendState;
	ID3D11SamplerState*		mD3DDefaultSamplerState;
	ID3D11SamplerState*		mD3DWrapSamplerState;
	bool					mNeedsReinitNative;

	ID3D11Buffer*			mMatrix2DBuffer;
	ID3D11Buffer*			mD3DVertexBuffer;
	ID3D11Buffer*			mD3DIndexBuffer;
	int						mVtxByteIdx;
	int						mIdxByteIdx;

	HashSet<DXRenderState*>	mRenderStates;
	HashSet<DXTexture*>		mTextures;
	HashSet<DXShader*>		mShaders;
	Dictionary<String, DXTexture*> mTextureMap;
	Dictionary<int, ID3D11Buffer*> mBufferMap;

public:
	virtual void			PhysSetRenderState(RenderState* renderState) override;
	virtual void			PhysSetRenderWindow(RenderWindow* renderWindow);
	virtual void			PhysSetRenderTarget(Texture* renderTarget) override;
	virtual RenderState*	CreateRenderState(RenderState* srcRenderState) override;
	virtual ModelInstance*	CreateModelInstance(ModelDef* modelDef, ModelCreateFlags flags) override;

public:
	DXRenderDevice();
	virtual ~DXRenderDevice();
	bool					Init(BFApp* app) override;

	void					ReleaseNative();
	void					ReinitNative();

	void					FrameStart() override;
	void					FrameEnd() override;

	Texture*				LoadTexture(const StringImpl& fileName, int flags) override;
	Texture*				LoadTexture(ImageData* imageData, int flags) override;
	Texture*				CreateDynTexture(int width, int height) override;
	Shader*					LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition) override;
	Texture*				CreateRenderTarget(int width, int height, bool destAlpha) override;

	void					SetRenderState(RenderState* renderState) override;
};

NS_BF_END;
