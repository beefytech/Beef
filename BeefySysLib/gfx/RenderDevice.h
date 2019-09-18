#pragma once

#include "Common.h"
#include "RenderTarget.h"
#include "util/Rect.h"
#include "util/SLIList.h"

NS_BF_BEGIN;

class Texture;
class Shader;
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

enum TextureFlag : int8
{
	TextureFlag_Additive = 1,
	TextureFlag_NoPremult = 2,
	TextureFlag_AllowRead = 4,
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

public:
	VertexDefinition()
	{
		mElementData = NULL;
		mNumElements = 0;
	}
	virtual ~VertexDefinition()
	{
		delete [] mElementData;
	}
};

class RenderState
{
public:
	Shader*					mShader;
	bool					mWriteDepthBuffer;
	DepthFunc				mDepthFunc;	
	bool					mClipped;
	Rect					mClipRect;
	CullMode				mCullMode;

public:
	RenderState();
	virtual ~RenderState() {}

	virtual void SetShader(Shader* shader) { mShader = shader; }
	virtual void SetClipped(bool clipped) { mClipped = clipped; }
	virtual void SetClipRect(const Rect& rect) { mClipRect = rect; }
	virtual void SetWriteDepthBuffer(bool writeDepthBuffer) { mWriteDepthBuffer = writeDepthBuffer; }
	virtual void SetDepthFunc(DepthFunc depthFunc) { mDepthFunc = depthFunc; }
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

class RenderDevice
{
public:	
	Array<DrawBatch*>		mDrawBatchPool;	
	
	RenderWindow*			mPhysRenderWindow;
	RenderState*			mPhysRenderState;
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

public:
	RenderDevice();
	virtual ~RenderDevice();
	virtual bool			Init(BFApp* app) = 0;	
	virtual void			AddRenderWindow(RenderWindow* renderWindow);
	virtual void			RemoveRenderWindow(RenderWindow* renderWindow);
	
	virtual RenderState*	CreateRenderState(RenderState* srcRenderState);
	virtual ModelInstance*	CreateModelInstance(ModelDef* modelDef) { return NULL; }
	virtual VertexDefinition* CreateVertexDefinition(VertexDefData* elementData, int numElements);	

	virtual void			FrameStart() = 0;
	virtual void			FrameEnd();

	virtual Texture*		LoadTexture(ImageData* imageData, int flags) = 0;
	virtual Texture*		CreateDynTexture(int width, int height) = 0;
	virtual Texture*		LoadTexture(const StringImpl& fileName, int flags);
	virtual Texture*		CreateRenderTarget(int width, int height, bool destAlpha) = 0;
	
	virtual Shader*			LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition) = 0;
		
	virtual void			SetRenderState(RenderState* renderState) = 0;
};

NS_BF_END;
