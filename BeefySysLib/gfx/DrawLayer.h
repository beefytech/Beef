#pragma once

#include "Common.h"
#include "util/SLIList.h"
#include "fbx/FBXReader.h"
#include "gfx/RenderCmd.h"
#include "gfx/RenderDevice.h"

NS_BF_BEGIN;

class RenderWindow;
class Texture;
class Shader;
class ShaderPass;
class BFApp;
class Vertex3D;
class DrawLayer;
class RenderState;

#define MAX_TEXTURES 4

class DrawBatch : public RenderCmd
{
public:
	DrawLayer*				mDrawLayer;
	int						mId;

	bool					mIsVertexBufferHead;
	bool					mIsIndexBufferHead;

	void*					mVertices;
	int						mVtxSize;
	int						mVtxIdx;
	int						mAllocatedVertices;

	uint16*					mIndices;
	int						mIdxIdx;
	int						mAllocatedIndices;

	Texture*				mCurTextures[MAX_TEXTURES];

public:
	DrawBatch();
	virtual ~DrawBatch();

	virtual void			Free() override;

	void					Clear();
	DrawBatch*				AllocateChainedBatch(int minVtxCount, int minIdxCount);
	virtual void*			AllocTris(int vtxCount);
	virtual void*			AllocStrip(int vtxCount);
	virtual void			AllocIndexed(int vtxCount, int idxCount, void** verticesOut, uint16** indicesOut, uint16* idxOfsOut);
};

#define DRAWBUFFER_CMDBUFFER_SIZE 64*1024



class DrawLayer
{
public:
	SLIList<RenderCmd*>		mRenderCmdList;
	DrawBatch*				mCurDrawBatch;

	RenderWindow*			mRenderWindow;
	RenderDevice*			mRenderDevice;

	void*					mIdxBuffer;
	void*					mVtxBuffer;
	void*					mRenderCmdBuffer;
	int						mIdxByteIdx;
	int						mVtxByteIdx;
	int						mRenderCmdByteIdx;

	Texture*				mCurTextures[MAX_TEXTURES];

public:
	template <typename T>
	T*						AllocRenderCmd(int extraBytes = 0)
	{
		if (mRenderCmdByteIdx + sizeof(T) + extraBytes >= DRAWBUFFER_CMDBUFFER_SIZE)
		{
			mRenderCmdBuffer = mRenderDevice->mPooledRenderCmdBuffers.AllocMemoryBlock();
			T* cmd = new(mRenderCmdBuffer) T();
			cmd->mIsPoolHead = true;
			mRenderCmdByteIdx = sizeof(T) + extraBytes;
			return cmd;
		}

		T* cmd = new((uint8*)mRenderCmdBuffer + mRenderCmdByteIdx) T();
		cmd->mIsPoolHead = false;
		mRenderCmdByteIdx += sizeof(T) + extraBytes;
		return cmd;
	}

public:
	void					CloseDrawBatch();
	virtual DrawBatch*		CreateDrawBatch() = 0;
	virtual DrawBatch*		AllocateBatch(int minVtxCount, int minIdxCount);
	void					QueueRenderCmd(RenderCmd* renderCmd);
	virtual RenderCmd*		CreateSetTextureCmd(int textureIdx, Texture* texture) = 0;
	virtual void			SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size) = 0;
	virtual void			SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount);

public:
	DrawLayer();
	virtual ~DrawLayer();

	virtual void			Draw();
	virtual void			Flush();
	virtual void			Clear();
	virtual void*			AllocTris(int vtxCount);
	virtual void*			AllocStrip(int vtxCount);
	virtual void			AllocIndexed(int vtxCount, int idxCount, void** verticesOut, uint16** indicesOut, uint16* idxOfsOut);
	virtual void			SetTexture(int texIdx, Texture* texture);
};

NS_BF_END;
