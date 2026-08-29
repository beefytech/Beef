#pragma once

#include "Common.h"
#include "RenderTarget.h"
#include "util/Rect.h"
#include "util/SLIList.h"

NS_BF_BEGIN;

class Texture;
class Shader;
class ComputeShader;
class ShaderPass;
class BFApp;

class DefaultVertex3D
{
public:
	float x;
	float y;
	float z;
	float u;
	float v;
	uint32 color;	

public:
	DefaultVertex3D()
	{
	}

	DefaultVertex3D(float _x, float _y, float _z, float _u, float _v, uint32 _color)
	{
		x = _x;
		y = _y;
		z = _z;
		color = _color;
		u = _u;
		v = _v;
	}

	void Set(float _x, float _y, float _z, float _u, float _v, uint32 _color)
	{
		x = _x;
		y = _y;
		z = _z;
		color = _color;
		u = _u;
		v = _v;
	}
};

class RenderWindow;
class DrawBatch;
class DrawLayer;
class BFWindow;
class ImageData;
class DrawLayer;
class ModelInstance;
class FBXReader;
class RenderCmd;
class ModelDef;

class RenderDevice;

class RenderWindow : public RenderTarget
{
public:
	RenderDevice*			mRenderDevice;
	BFWindow*				mWindow;	
	Array<DrawLayer*>		mDrawLayerList;
	DrawLayer*				mCurDrawLayer;
		
public:
	RenderWindow();
	virtual ~RenderWindow();

	virtual void			SetAsTarget() = 0;
	virtual void			Resized() = 0;
	virtual void			Present() = 0;
	virtual float			GetRefreshRate() { return 60.0f; }
	virtual bool			WaitForVBlank() { return false; }
};

const int DRAWBUFFER_IDXBUFFER_SIZE = 8*1024;
const int DRAWBUFFER_VTXBUFFER_SIZE = 64*1024;

enum DepthFunc : int8
{
	DepthFunc_Never,
	DepthFunc_Less,
	DepthFunc_LessEqual,
	DepthFunc_Equal,
	DepthFunc_Greater,
	DepthFunc_NotEqual,
	DepthFunc_GreaterEqual,
	DepthFunc_Always
};

enum VertexElementFormat : int8
{
	VertexElementFormat_Single,
	VertexElementFormat_Vector2,
	VertexElementFormat_Vector3,
	VertexElementFormat_Vector4,
	VertexElementFormat_Color,
	VertexElementFormat_Byte4,
	VertexElementFormat_Short2,
	VertexElementFormat_Short4,
	VertexElementFormat_NormalizedShort2,
	VertexElementFormat_NormalizedShort4,
	VertexElementFormat_HalfVector2,
	VertexElementFormat_HalfVector4
};

enum VertexElementUsage : int8
{
	VertexElementUsage_Position2D,
	VertexElementUsage_Position3D,
	VertexElementUsage_Color,
	VertexElementUsage_TextureCoordinate,
	VertexElementUsage_Normal,
	VertexElementUsage_Binormal,
	VertexElementUsage_Tangent,
	VertexElementUsage_BlendIndices,
	VertexElementUsage_BlendWeight,
	VertexElementUsage_Depth,
	VertexElementUsage_Fog,
	VertexElementUsage_PointSize,
	VertexElementUsage_Sample,
	VertexElementUsage_TessellateFactor
};

enum ConstantDataType : int8
{
	ConstantDataType_Single,
	ConstantDataType_Vector2,
	ConstantDataType_Vector3,
	ConstantDataType_Vector4,
	ConstantDataType_Matrix
};

enum CullMode : int8
{
	CullMode_None,
	CullMode_Front,
	CullMode_Back
};

enum FrontFace : int8
{
	FrontFace_Clockwise,
	FrontFace_CounterClockwise
};

enum Topology3D : int8
{
	Topology3D_TriangleList,
	Topology3D_LineLine
};

// Nearest always clamps (there's no "nearest + wrap" combination in use).
enum SamplerKind : int8
{
	SamplerKind_Wrap,
	SamplerKind_Clamp,
	SamplerKind_Nearest
};

enum TextureFlag : int8
{
	TextureFlag_Additive = 1,
	TextureFlag_NoPremult = 2,
	TextureFlag_AllowRead = 4,
	TextureFlag_HasTransFollowing = 8,
	TextureFlag_Mipmaps = 0x10,
	// Color data: store sRGB-encoded, sample hardware-decoded to linear.
	TextureFlag_Srgb = 0x20,
	TextureFlag_UseLoadCache = 0x40
};

struct VertexDefData
{
	VertexElementUsage mUsage;
	int mUsageIndex;
	VertexElementFormat mFormat;
};

class VertexDefinition
{
public:
	VertexDefData* mElementData;
	int mNumElements;
	// Element that an instanced draw feeds from a per-instance stream instead of the vertex (-1 = none).
	// Shaders built on this definition get a second input layout for it (see DXShader::mD3DInstLayout).
	int mInstanceElementIdx;

public:
	VertexDefinition()
	{
		mElementData = NULL;
		mNumElements = 0;
		mInstanceElementIdx = -1;
	}

	VertexDefinition(VertexDefinition* src)
	{
		mElementData = new VertexDefData[src->mNumElements];
		mNumElements = src->mNumElements;
		memcpy(mElementData, src->mElementData, sizeof(VertexDefData) * mNumElements);
		mInstanceElementIdx = src->mInstanceElementIdx;
	}

	virtual ~VertexDefinition()
	{
		delete [] mElementData;
	}
};

// GPU-resident geometry uploaded once (see RenderDevice::CreateStaticMesh), drawn instanced through
// DrawLayer::DrawStaticMeshInstanced.
class StaticMesh
{
public:
	int mVtxSize;
	int mVtxCount;
	int mIdxCount;
	bool mIdx32;

public:
	StaticMesh()
	{
		mVtxSize = 0;
		mVtxCount = 0;
		mIdxCount = 0;
		mIdx32 = false;
	}
	virtual ~StaticMesh() {}
};

// One timed GPU region (see RenderDevice::GpuTimerSpanBegin): mTag is whatever the caller last set
// with GpuTimerSetTag, so the consumer can attribute the time to its own profiler section.
struct GpuTimerSpan
{
	int mTag;
	int64 mNanos;
};

class RenderState
{
public:
	Shader*					mShader;
	bool					mWriteDepthBuffer;
	DepthFunc				mDepthFunc;
	bool					mClipped;
	SamplerKind				mSamplerKind;
	bool					mWireframe;
	RectF					mClipRect;
	CullMode				mCullMode;
	FrontFace				mFrontFace;
	Topology3D				mTopology;	
	bool					mDisablePixelShader;
	bool					mDisableRenderTarget;
	bool					mDisableBlend;
	bool					mAlphaToCoverage;

public:
	RenderState();
	virtual ~RenderState() {}

	virtual void SetShader(Shader* shader) { mShader = shader; }
	virtual void SetSamplerKind(SamplerKind samplerKind) { mSamplerKind = samplerKind; }
	virtual void SetWireframe(bool wireframe) { mWireframe = wireframe; }
	virtual void SetClipped(bool clipped) { mClipped = clipped; }
	virtual void SetClipRect(const RectF& rect) { mClipRect = rect; }
	virtual void SetWriteDepthBuffer(bool writeDepthBuffer) { mWriteDepthBuffer = writeDepthBuffer; }
	virtual void SetDepthFunc(DepthFunc depthFunc) { mDepthFunc = depthFunc; }
	virtual void SetTopology(Topology3D topology) { mTopology = topology; }
	virtual void SetCullMode(CullMode cullMode) { mCullMode = cullMode; }
	virtual void SetFrontFace(FrontFace frontFace) { mFrontFace = frontFace; }
	virtual void SetDisablePixelShader(bool disable) { mDisablePixelShader = disable; }
	virtual void SetDisableRenderTarget(bool disable) { mDisableRenderTarget = disable; }
	virtual void SetDisableBlend(bool disable) { mDisableBlend = disable; }
	virtual void SetAlphaToCoverage(bool enabled) { mAlphaToCoverage = enabled; }
};

class PoolData
{
public:
	PoolData* mNext;
};

class MemoryPool : protected SLIList<PoolData*>
{
public:
	int mSize;

public:
	MemoryPool(int size)
	{
		mSize = size;
	}

	~MemoryPool()
	{
		auto cur = mHead;
		while (cur != NULL)
		{
			auto next = cur->mNext;
			delete [] cur;
			cur = next;
		}
	}

	void* AllocMemoryBlock()
	{
		if (IsEmpty())		
			return new uint8[mSize];		
		return (uint8*)PopFront();
	}

	void FreeMemoryBlock(void* block)
	{
		PoolData* poolData = (PoolData*)block;
		poolData->mNext = NULL;
		PushBack(poolData);
	}
};

enum ModelCreateFlags
{
	ModelCreateFlags_None = 0,
	ModelCreateFlags_NoSetRenderState = 1
};

// Search directories for shader #include resolution, tried after the including file's own
// directory. The shader cache's include hash walk uses the same list.
void AddShaderIncludeDir(const StringImpl& dir);
const Array<String>& GetShaderIncludeDirs();

class RenderDevice
{
public:
	Array<DrawBatch*>		mDrawBatchPool;
	
	BFApp*					mApp;
	RenderWindow*			mPhysRenderWindow;
	RenderState*			mPhysRenderState;
	// Sample count for window swapchains/backbuffers (blt-model MSAA, resolved by Present) -- must
	// be set before window creation; validated against hardware support there.
	int						mWindowMsaaSampleCount;
	int						mResizeCount;
	Array<RenderWindow*>	mRenderWindowList;
	RenderTarget*			mCurRenderTarget;
	DrawLayer*				mCurDrawLayer;	

	RenderState*			mDefaultRenderState;
	RenderState*			mCurRenderState;	

	MemoryPool				mPooledIndexBuffers;
	MemoryPool				mPooledVertexBuffers;
	MemoryPool				mPooledRenderCmdBuffers;

public:	
	virtual void			PhysSetRenderState(RenderState* renderState) = 0;
	virtual void			PhysSetRenderTarget(Texture* renderTarget) = 0;
	// Restricts rasterization of the current target to a pixel rect (reset by the next target set);
	// `clear` clears just that rect first (color + depth).
	virtual void			PhysSetViewportRect(int x, int y, int width, int height, bool clear) {}

public:
	RenderDevice();
	virtual ~RenderDevice();
	virtual bool			Init(BFApp* app) = 0;
	virtual void			AddRenderWindow(RenderWindow* renderWindow);
	virtual void			RemoveRenderWindow(RenderWindow* renderWindow);
	
	virtual RenderState*	CreateRenderState(RenderState* srcRenderState);
	virtual void			ReleaseRenderState(RenderState* renderState);

	virtual ModelInstance*	CreateModelInstance(ModelDef* modelDef, ModelCreateFlags flags) { return NULL; }
	virtual VertexDefinition* CreateVertexDefinition(VertexDefData* elementData, int numElements);	

	// GPU timing. Spans are bracketed around actual submissions (layer flushes, resolves) rather than
	// around caller code, since queued draws don't execute where they were recorded. Results are read
	// back a few frames later (GpuTimerFetch), never waited on.
	virtual void			GpuTimerSetEnabled(bool enabled) {}
	virtual bool			GpuTimerBeginFrame(int64 frameId) { return false; }
	virtual void			GpuTimerSetTag(int tag) {}
	virtual int				GpuTimerSpanBegin() { return -1; }
	virtual void			GpuTimerSpanEnd(int spanId) {}
	virtual void			GpuTimerEndFrame() {}
	// -1 = the oldest frame's results aren't ready yet; otherwise the span count for *outFrameId.
	virtual int				GpuTimerFetch(int64* outFrameId, GpuTimerSpan* outSpans, int maxSpans) { return -1; }
	// idxData is uint16 or uint32 per idx32. Delete the mesh only after every layer that queued draws of it has flushed.
	virtual StaticMesh*		CreateStaticMesh(int vertexSize, void* vtxData, int vtxCount, void* idxData, int idxCount, bool idx32) { return NULL; }

	virtual void			FrameStart() = 0;
	virtual void			FrameEnd();

	virtual Texture*		LoadTexture(ImageData* imageData, int flags) = 0;
	virtual Texture*		CreateDynTexture(int width, int height) = 0;
	virtual Texture*		LoadTexture(const StringImpl& fileName, int flags);
	virtual Texture*		CreateRenderTarget(int width, int height, int flags, int sampleCount) = 0;
	// Depth-only target: no color plane; the depth buffer itself is the sampleable resource.
	virtual Texture*		CreateDepthTarget(int width, int height, bool is16Bit) { return NULL; }
	// GPU structured buffer (StructuredBuffer<T> in HLSL) bound through the texture slots. Flags:
	// 1 = GPU-writable (RWStructuredBuffer via a compute UAV; SetBufferData uploads instead of maps).
	// 2 = CPU-updatable in place (default usage, Texture::UpdateBufferRange writes sub-ranges immediately).
	virtual Texture*		CreateStructuredBuffer(int stride, int count, int flags = 0) { return NULL; }
	// Volume texture with a UAV per mip (RWTexture3D); flags share CreateRenderTarget's format bits.
	virtual Texture*		CreateTexture3D(int width, int height, int depth, int flags) { return NULL; }
	virtual Texture*		OpenSharedRenderTarget(void* handle, int width, int height) { return NULL; }
	
	// entrySuffix compiles alternate entry points ("VS"+suffix / "PS"+suffix) from the same file --
	// how one surface-shader source yields its per-pass variants.
	virtual Shader*			LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition, const StringImpl& entrySuffix) = 0;
	virtual void			ReleaseShader(Shader* shader);
	virtual ComputeShader*	LoadComputeShader(const StringImpl& fileName, const StringImpl& entry) { return NULL; }
	virtual void			ReleaseComputeShader(ComputeShader* shader);
		
	virtual void			SetRenderState(RenderState* renderState) = 0;
};

NS_BF_END;
