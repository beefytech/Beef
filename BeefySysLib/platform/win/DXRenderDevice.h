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
#include <d3d11_1.h>
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
	IDXGIKeyedMutex*		mD3DKeyedMutex;
	// Mip-0 unordered access view when created GPU-writable (see GetUAV); NULL otherwise.
	ID3D11UnorderedAccessView* mD3DUAV;
	uint32*					mContentBits;
	DXGI_FORMAT				mD3DFormat;
	int						mSampleCount;
	// Scene depth is reverse-Z (cleared to 0); shadow atlases stay standard-Z (cleared to 1).
	bool					mStandardDepthClear;

public:
	DXTexture();
	~DXTexture();

	void					ReleaseNative();
	void					ReinitNative();

	virtual ID3D11UnorderedAccessView* GetUAV(int mipLevel) { return (mipLevel == 0) ? mD3DUAV : NULL; }

	virtual void			PhysSetAsTarget() override;
	virtual void			Blt(ImageData* imageData, int x, int y) override;
	virtual void			SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits) override;
	virtual void			GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) override;
	virtual void			GetDepthBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) override;
	virtual Texture*		CreateDepthRef() override;
	virtual Texture*		CreateRawRef() override;
	virtual void*			GetSharedHandle() override;
	virtual bool			AcquireKeyedMutex(uint64 key, uint32 timeoutMs) override;
	virtual void			ReleaseKeyedMutex(uint64 key) override;
	virtual void			ResolveTo(Texture* dest) override;
	virtual void			GenerateMips() override;
	virtual void			CopyToMip(int mipLevel, Texture* src, int width, int height) override;
};

// Structured buffer posing as a texture: mD3DResourceView is the buffer SRV, so DXSetTextureCmd
// binds it into a t-slot unchanged; every other DXTexture member stays NULL. mWidth = element count.
// GPU-writable buffers are USAGE_DEFAULT with a UAV (mD3DUAV) and upload via UpdateSubresource
// instead of Map; mD3DStaging is created on the first readback.
class DXStructuredBuffer : public DXTexture
{
public:
	ID3D11Buffer*			mD3DBuffer;
	ID3D11Buffer*			mD3DStaging;
	int						mStride;
	bool					mGpuWritable;
	bool					mDefaultUsage; // GPU-writable or CPU-updatable: written with UpdateSubresource, never mapped

public:
	DXStructuredBuffer();
	~DXStructuredBuffer();

	virtual void			PhysSetAsTarget() override;
	virtual bool			GetBufferData(void* outData, int size) override;
	virtual void			UpdateBufferRange(int offset, void* data, int size) override;
};

// Volume texture: SRV over the whole mip chain, one UAV per mip for compute writes. Not a render
// target. mWidth/mHeight/mDepth are mip 0.
class DXTexture3D : public DXTexture
{
public:
	static const int		cMaxMips = 16;

	ID3D11Texture3D*		mD3DTexture3D;
	ID3D11Texture3D*		mD3DStaging;
	ID3D11UnorderedAccessView* mD3DUAVs[cMaxMips];
	int						mDepth;
	int						mMipLevels;
	int						mBytesPerTexel;

public:
	DXTexture3D();
	~DXTexture3D();

	virtual ID3D11UnorderedAccessView* GetUAV(int mipLevel) override { return ((mipLevel >= 0) && (mipLevel < mMipLevels)) ? mD3DUAVs[mipLevel] : NULL; }
	virtual void			PhysSetAsTarget() override;
	virtual void			SetData3D(int mipLevel, void* data, int rowPitch, int slicePitch) override;
	virtual bool			GetData3D(int mipLevel, void* outData, int outSize) override;
	virtual void			GenerateMips() override;
};

class DXComputeShader : public ComputeShader
{
public:
	DXRenderDevice*			mRenderDevice;
	String					mSrcPath;
	String					mEntry;
	ID3D11ComputeShader*	mD3DComputeShader;

public:
	DXComputeShader();
	~DXComputeShader();

	bool					Load();
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
	ID3D11InputLayout*		mD3DInstLayout; // the instance element from slot 1 (per-instance); NULL if the vertex def has none
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
	virtual void			SetBufferData(Texture* buffer, void* data, int size) override;
	virtual void			SetComputeTexture(int slot, Texture* texture) override;
	virtual void			SetComputeUAV(int slot, Texture* texture, int mipLevel) override;
	virtual void			Dispatch(ComputeShader* shader, int groupsX, int groupsY, int groupsZ) override;
	virtual void			SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size) override;
	virtual void			SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount) override;
	virtual void			DrawStaticMeshInstanced(StaticMesh* mesh, int instBase, int instCount) override;

public:
	DXDrawLayer();
	~DXDrawLayer();
};

class DXStaticMesh : public StaticMesh
{
public:
	ID3D11Buffer*			mD3DVertexBuffer;
	ID3D11Buffer*			mD3DIndexBuffer;

public:
	DXStaticMesh();
	~DXStaticMesh();
};

class DXStaticMeshDrawCmd : public RenderCmd
{
public:
	DXStaticMesh*			mMesh;
	int						mInstBase;
	int						mInstCount;

public:
	virtual void CommandQueued(DrawLayer* drawLayer) override;
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
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
#define DX_VS_TEXTURE_SLOT 24
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
	virtual void SetSamplerKind(SamplerKind samplerKind);
	virtual void SetClipRect(const RectF& rect);
	virtual void SetWriteDepthBuffer(bool writeDepthBuffer);
	virtual void SetDepthFunc(DepthFunc depthFunc);
	virtual void SetCullMode(CullMode cullMode);
	virtual void SetFrontFace(FrontFace frontFace);
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

	virtual void CommandQueued(RenderCmd* renderCmd, DrawLayer* drawLayer) override;
	virtual void Render(RenderCmd* renderCmd, RenderDevice* renderDevice, RenderWindow* renderWindow) override;
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

// Heap-owned copy of the data: uploads can exceed the 64K command pool block.
class DXSetBufferDataCmd : public RenderCmd
{
public:
	DXStructuredBuffer* mBuffer;
	uint8* mData;
	int mSize;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
	virtual void Free() override;
};

class DXSetComputeTextureCmd : public RenderCmd
{
public:
	int mSlot;
	DXTexture* mTexture;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXSetComputeUAVCmd : public RenderCmd
{
public:
	int mSlot;
	int mMipLevel;
	DXTexture* mTexture;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class DXDispatchCmd : public RenderCmd
{
public:
	DXComputeShader* mShader;
	int mGroupsX;
	int mGroupsY;
	int mGroupsZ;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

// D3D11 timestamp queries for one frame: a disjoint query covering the frame plus a begin/end pair
// per span. Frames are read back from a ring so the CPU never waits on the GPU.
class DXGpuTimerFrame
{
public:
	ID3D11Query*			mDisjoint;
	Array<ID3D11Query*>		mBeginQueries;
	Array<ID3D11Query*>		mEndQueries;
	Array<int>				mTags;
	int						mSpanCount;
	int64					mFrameId;
	bool					mOpen;
	bool					mPending;

public:
	DXGpuTimerFrame();
	~DXGpuTimerFrame();
	void ReleaseNative();
};

#define DX_GPUTIMER_FRAMES 4
#define DX_GPUTIMER_MAX_SPANS 256

class DXRenderDevice : public RenderDevice
{
public:
	IDXGIFactory*			mDXGIFactory;
	ID3D11Device*			mD3DDevice;
	ID3D11DeviceContext*	mD3DDeviceContext;
	ID3D11DeviceContext1*	mD3DDeviceContext1; // 11.1 interface for rect clears; NULL on 11.0 runtimes
	ID3D11BlendState*		mD3DNormalBlendState;
	ID3D11SamplerState*		mD3DDefaultSamplerState;
	ID3D11SamplerState*		mD3DWrapSamplerState;
	ID3D11SamplerState*		mD3DNearestSamplerState;
	ID3D11SamplerState*		mD3DShadowSamplerState;
	ID3D11SamplerState*		mD3DTrilinearSamplerState;
	bool					mNeedsReinitNative;

	ID3D11Buffer*			mMatrix2DBuffer;
	ID3D11Buffer*			mD3DVertexBuffer;
	ID3D11Buffer*			mD3DIndexBuffer;
	int						mVtxByteIdx;
	int						mIdxByteIdx;
	// Per-instance stream for DXStaticMeshDrawCmd: float k+1 at element k, so an instanced draw bound at
	// offset instBase*4 feeds instance i the value instBase+i+1 (the same encoding as a stamped vertex).
	ID3D11Buffer*			mInstIotaBuffer;
	int						mInstIotaCount;
	
	ID3D11RenderTargetView*	mCurD3DRTV;
	ID3D11DepthStencilView*	mCurD3DDSV;

	DXGpuTimerFrame			mGpuTimerFrames[DX_GPUTIMER_FRAMES];
	int						mGpuTimerWriteIdx;
	int						mGpuTimerCurTag;
	bool					mGpuTimerEnabled;

	HashSet<DXRenderState*>	mRenderStates;
	HashSet<DXTexture*>		mTextures;
	HashSet<DXShader*>		mShaders;
	HashSet<DXComputeShader*> mComputeShaders;
	Dictionary<String, DXTexture*> mTextureMap;
	Dictionary<int, ID3D11Buffer*> mBufferMap;
	// Compute slots bound since the last dispatch (bit per slot); the dispatch unbinds them.
	uint32					mCSBoundSRVs;
	uint32					mCSBoundUAVs;

public:
	virtual void			PhysSetRenderState(RenderState* renderState) override;
	virtual void			PhysSetRenderWindow(RenderWindow* renderWindow);
	virtual void			PhysSetRenderTarget(Texture* renderTarget) override;
	virtual void			PhysSetViewportRect(int x, int y, int width, int height, bool clear) override;
	virtual RenderState*	CreateRenderState(RenderState* srcRenderState) override;
	virtual void			ReleaseRenderState(RenderState* renderState) override;
	virtual ModelInstance*	CreateModelInstance(ModelDef* modelDef, ModelCreateFlags flags) override;
	virtual StaticMesh*		CreateStaticMesh(int vertexSize, void* vtxData, int vtxCount, void* idxData, int idxCount, bool idx32) override;
	virtual void			GpuTimerSetEnabled(bool enabled) override;
	virtual bool			GpuTimerBeginFrame(int64 frameId) override;
	virtual void			GpuTimerSetTag(int tag) override;
	virtual int				GpuTimerSpanBegin() override;
	virtual void			GpuTimerSpanEnd(int spanId) override;
	virtual void			GpuTimerEndFrame() override;
	virtual int				GpuTimerFetch(int64* outFrameId, GpuTimerSpan* outSpans, int maxSpans) override;
	ID3D11Query*			GetTimestampQuery(Array<ID3D11Query*>& queries, int idx);
	void					EnsureInstIota(int count);

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
	void					ReleaseShader(Shader* shader) override;
	Texture*				CreateRenderTarget(int width, int height, int flags, int sampleCount) override;
	Texture*				CreateDepthTarget(int width, int height, bool is16Bit) override;
	Texture*				CreateStructuredBuffer(int stride, int count, int flags) override;
	Texture*				CreateTexture3D(int width, int height, int depth, int flags) override;
	Texture*				OpenSharedRenderTarget(void* handle, int width, int height) override;
	ComputeShader*			LoadComputeShader(const StringImpl& fileName, const StringImpl& entry) override;
	void					ReleaseComputeShader(ComputeShader* shader) override;

	void					SetRenderState(RenderState* renderState) override;
};

NS_BF_END;
