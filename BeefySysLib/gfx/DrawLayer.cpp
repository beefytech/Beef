#include "BFApp.h"
#include "gfx/DrawLayer.h"
#include "gfx/Texture.h"
#include "gfx/RenderDevice.h"
#include "gfx/Shader.h"
#include "util/PerfTimer.h"
#include "util/BeefPerf.h"

USING_NS_BF;

static int sCurBatchId = 0;

DrawBatch::DrawBatch()
{
	mId = ++sCurBatchId;
	mVtxIdx = 0;
	mIdxIdx = 0;
	mAllocatedVertices = 0;
	mAllocatedIndices = 0;
	mIsIndexBufferHead = false;
	mIsVertexBufferHead = false;
	mVertices = NULL;
	mIndices = NULL;
	mRenderState = NULL;
	mDrawLayer = NULL;
	mNext = NULL;
	Clear();
}

DrawBatch::~DrawBatch()
{
}


void DrawBatch::Clear()
{
	mVtxIdx = 0;
	mIdxIdx = 0;
	for (int texIdx = 0; texIdx < MAX_TEXTURES; texIdx++)
		mCurTextures[texIdx] = (Texture*)(intptr)-1;
}

void DrawBatch::Free()
{
	RenderDevice* renderDevice = mDrawLayer->mRenderDevice;

	if (mIsVertexBufferHead)
		renderDevice->mPooledVertexBuffers.FreeMemoryBlock(mVertices);
	if (mIsIndexBufferHead)
		renderDevice->mPooledIndexBuffers.FreeMemoryBlock(mIndices);

	mIsVertexBufferHead = false;
	mIsIndexBufferHead = false;

	Clear();
	auto& pool = renderDevice->mDrawBatchPool;
	pool.push_back(this);
}


DrawBatch* DrawBatch::AllocateChainedBatch(int minVtxCount, int minIdxCount)
{
	mDrawLayer->CloseDrawBatch();
	return mDrawLayer->AllocateBatch(minVtxCount, minIdxCount);
}

void* DrawBatch::AllocTris(int vtxCount)
{
	int idxCount = vtxCount;

	if ((mRenderState != gBFApp->mRenderDevice->mCurRenderState) || (idxCount + mIdxIdx >= mAllocatedIndices))
	{
		if (mVtxIdx > 0)
		{
			DrawBatch* nextBatch = AllocateChainedBatch(0, 0);
			return nextBatch->AllocTris(vtxCount);
		}

		mRenderState = gBFApp->mRenderDevice->mCurRenderState;
	}

	uint16* idxPtr = mIndices + mIdxIdx;
	void* vtxPtr = (uint8*)mVertices + (mVtxIdx * mVtxSize);

	for (int idxNum = 0; idxNum < idxCount; idxNum++)
	{
		*idxPtr++ = mVtxIdx++;
		mIdxIdx++;
	}

	return vtxPtr;
}

void* DrawBatch::AllocStrip(int vtxCount)
{
	int idxCount = (vtxCount - 2) * 3;

	if ((mRenderState != gBFApp->mRenderDevice->mCurRenderState) || (idxCount + mIdxIdx >= mAllocatedIndices))
	{
		if (mVtxIdx > 0)
		{
			DrawBatch* nextBatch = AllocateChainedBatch(0, 0);
			return nextBatch->AllocStrip(vtxCount);
		}

		mRenderState = gBFApp->mRenderDevice->mCurRenderState;
	}

	uint16* idxPtr = mIndices + mIdxIdx;

	void* vtxPtr = (uint8*)mVertices + (mVtxIdx * mVtxSize);

	mVtxIdx += 2;

	for (int idxNum = 0; idxNum < idxCount; idxNum += 3)
	{
		*idxPtr++ = mVtxIdx - 2;
		*idxPtr++ = mVtxIdx - 1;
		*idxPtr++ = mVtxIdx;
		mVtxIdx++;
		mIdxIdx += 3;
	}

	return vtxPtr;
}

void DrawBatch::AllocIndexed(int vtxCount, int idxCount, void** verticesOut, uint16** indicesOut, uint16* idxOfsOut)
{
	if ((mRenderState != gBFApp->mRenderDevice->mCurRenderState) || (idxCount + mIdxIdx > mAllocatedIndices))
	{
		if (mVtxIdx > 0)
		{
			DrawBatch* nextBatch = AllocateChainedBatch(vtxCount, idxCount);
			return nextBatch->AllocIndexed(vtxCount, idxCount, verticesOut, indicesOut, idxOfsOut);
		}

		mRenderState = gBFApp->mRenderDevice->mCurRenderState;
	}

	*verticesOut = (uint8*)mVertices + (mVtxIdx * mVtxSize);
	*indicesOut = mIndices + mIdxIdx;
	*idxOfsOut = mVtxIdx;

	mVtxIdx += vtxCount;
	mIdxIdx += idxCount;
}

//

DrawLayer::DrawLayer()
{
	mRenderWindow = NULL;
	mRenderDevice = NULL;
	mIdxBuffer = NULL;
	mVtxBuffer = NULL;
	mRenderCmdBuffer = NULL;
	mIdxByteIdx = 0;
	mVtxByteIdx = 0;
	mRenderCmdByteIdx = 0;
	mCurDrawBatch = NULL;
	for (int textureIdx = 0; textureIdx < MAX_TEXTURES; textureIdx++)
		mCurTextures[textureIdx] = NULL;
}

DrawLayer::~DrawLayer()
{
}

void DrawLayer::CloseDrawBatch()
{
	if (mCurDrawBatch == NULL)
		return;
	mIdxByteIdx += mCurDrawBatch->mIdxIdx * sizeof(uint16);
	mVtxByteIdx += mCurDrawBatch->mVtxIdx * mCurDrawBatch->mVtxSize;
	BF_ASSERT(mVtxByteIdx <= DRAWBUFFER_VTXBUFFER_SIZE);
	mCurDrawBatch = NULL;
}

void DrawLayer::QueueRenderCmd(RenderCmd* renderCmd)
{
	CloseDrawBatch();
	mRenderCmdList.PushBack(renderCmd);
	renderCmd->CommandQueued(this);
}

DrawBatch* DrawLayer::AllocateBatch(int minVtxCount, int minIdxCount)
{
	BP_ZONE("DrawLayer::AllocateBatch");

	BF_ASSERT(mRenderDevice->mCurRenderState->mShader != NULL);
	int vtxSize = mRenderDevice->mCurRenderState->mShader->mVertexSize;

	if (minIdxCount == 0)
	{
		minIdxCount = 512;
		minVtxCount = minIdxCount;
	}

	BF_ASSERT(minIdxCount * sizeof(uint16) <= DRAWBUFFER_IDXBUFFER_SIZE);
	BF_ASSERT(minVtxCount * vtxSize <= DRAWBUFFER_VTXBUFFER_SIZE);

	DrawBatch* drawBatch = NULL;

	auto& pool = mRenderDevice->mDrawBatchPool;
	if (pool.size() == 0)
	{
		drawBatch = CreateDrawBatch();
	}
	else
	{
		drawBatch = pool.back();
		pool.pop_back();
	}
	drawBatch->mDrawLayer = this;

	int needIdxBytes = minIdxCount * sizeof(uint16);
	int needVtxBytes = minVtxCount * vtxSize;

	if (needVtxBytes < DRAWBUFFER_VTXBUFFER_SIZE - mVtxByteIdx)
	{
		//mVtxByteIdx = ((mVtxByteIdx + vtxSize - 1) / vtxSize) * vtxSize;
		drawBatch->mVertices = (Vertex3D*)((uint8*) mVtxBuffer + mVtxByteIdx);
		drawBatch->mAllocatedVertices = (int)((DRAWBUFFER_VTXBUFFER_SIZE - mVtxByteIdx) / vtxSize);
		drawBatch->mIsVertexBufferHead = false;
	}
	else
	{
		mVtxBuffer = mRenderDevice->mPooledVertexBuffers.AllocMemoryBlock();
		mVtxByteIdx = 0;
		drawBatch->mVertices = (Vertex3D*)mVtxBuffer;
		drawBatch->mAllocatedVertices = DRAWBUFFER_VTXBUFFER_SIZE / vtxSize;
		drawBatch->mIsVertexBufferHead = true;
	}

	if (needIdxBytes < DRAWBUFFER_IDXBUFFER_SIZE - mIdxByteIdx)
	{
		drawBatch->mIndices = (uint16*)((uint8*)mIdxBuffer + mIdxByteIdx);
		drawBatch->mAllocatedIndices = (DRAWBUFFER_IDXBUFFER_SIZE - mIdxByteIdx) / sizeof(uint16);
		drawBatch->mIsIndexBufferHead = false;
	}
	else
	{
		mIdxBuffer = mRenderDevice->mPooledIndexBuffers.AllocMemoryBlock();
		mIdxByteIdx = 0;
		drawBatch->mIndices = (uint16*)mIdxBuffer;
		drawBatch->mAllocatedIndices = DRAWBUFFER_IDXBUFFER_SIZE / sizeof(uint16);
		drawBatch->mIsIndexBufferHead = true;
	}

	drawBatch->mAllocatedIndices = std::min(drawBatch->mAllocatedVertices, drawBatch->mAllocatedIndices);
	drawBatch->mVtxSize = vtxSize;

	mRenderCmdList.PushBack(drawBatch);
	mCurDrawBatch = drawBatch;

	return drawBatch;
}

void DrawLayer::Draw()
{
	BP_ZONE("DrawLayer::Draw");

	RenderCmd* curRenderCmd = mRenderCmdList.mHead;
	while (curRenderCmd != NULL)
	{
		curRenderCmd->Render(mRenderDevice, mRenderWindow);
		curRenderCmd = curRenderCmd->mNext;
	}
}

void DrawLayer::Flush()
{
	Draw();
	Clear();
}

void DrawLayer::Clear()
{
	for (int texIdx = 0; texIdx < MAX_TEXTURES; texIdx++)
		mCurTextures[texIdx] = (Texture*) (intptr) -1;

	RenderCmd* curBatch = mRenderCmdList.mHead;
	while (curBatch != NULL)
	{
		RenderCmd* nextBatch = curBatch->mNext;
		curBatch->Free();
		curBatch = nextBatch;
	}

	/*if ((mIdxBuffer == NULL) || (mCurDrawBatch != NULL))
		mIdxBuffer = mRenderDevice->mPooledIndexBuffers.AllocMemoryBlock();
	if ((mVtxBuffer == NULL) || (mCurDrawBatch != NULL))
		mVtxBuffer = mRenderDevice->mPooledVertexBuffers.AllocMemoryBlock();
	if ((mRenderCmdBuffer == NULL) || (mRenderCmdByteIdx != 0))
		mRenderCmdBuffer = mRenderDevice->mPooledRenderCmdBuffers.AllocMemoryBlock();
	mIdxByteIdx = 0;
	mVtxByteIdx = 0;
	mRenderCmdByteIdx = 0;*/

	mIdxBuffer = NULL;
	mVtxByteIdx = 0;
	mRenderCmdBuffer = NULL;
	mIdxByteIdx = DRAWBUFFER_IDXBUFFER_SIZE;
	mVtxByteIdx = DRAWBUFFER_VTXBUFFER_SIZE;
	mRenderCmdByteIdx = DRAWBUFFER_CMDBUFFER_SIZE;

	mRenderCmdList.Clear();
	mCurDrawBatch = NULL;
}

void* DrawLayer::AllocTris(int vtxCount)
{
	if (mCurDrawBatch == NULL)
		AllocateBatch(0, 0);
	return mCurDrawBatch->AllocTris(vtxCount);
}

void* DrawLayer::AllocStrip(int vtxCount)
{
	if (mCurDrawBatch == NULL)
		AllocateBatch(0, 0);
	return mCurDrawBatch->AllocStrip(vtxCount);
}

void DrawLayer::AllocIndexed(int vtxCount, int idxCount, void** verticesOut, uint16** indicesOut, uint16* idxOfsOut)
{
	if (mCurDrawBatch == NULL)
		AllocateBatch(0, 0);
	mCurDrawBatch->AllocIndexed(vtxCount, idxCount, verticesOut, indicesOut, idxOfsOut);
}

void DrawLayer::SetTexture(int texIdx, Texture* texture)
{
	if (mCurTextures[texIdx] != texture)
	{
		QueueRenderCmd(CreateSetTextureCmd(texIdx, texture));
		mCurTextures[texIdx] = texture;
	}
}

void DrawLayer::SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount)
{
	SetShaderConstantData(usageIdx, slotIdx, constData, size);
}

///

BF_EXPORT DrawLayer* BF_CALLTYPE DrawLayer_Create(BFWindow* window)
{
	DrawLayer* aDrawLayer = gBFApp->CreateDrawLayer(window);
	aDrawLayer->Clear();
	return aDrawLayer;
}

BF_EXPORT void BF_CALLTYPE DrawLayer_Delete(DrawLayer* drawLayer)
{
	if (drawLayer->mRenderWindow != NULL)
	{
		drawLayer->mRenderWindow->mDrawLayerList.Remove(drawLayer);
	}

	delete drawLayer;
}

BF_EXPORT void BF_CALLTYPE DrawLayer_Clear(DrawLayer* drawLayer)
{
	drawLayer->Clear();
}

BF_EXPORT void BF_CALLTYPE DrawLayer_Activate(DrawLayer* drawLayer)
{
	if (drawLayer->mRenderWindow != NULL)
	{
		drawLayer->mRenderWindow->SetAsTarget();
	}
	else
	{
		gBFApp->mRenderDevice->mCurRenderTarget = NULL;
	}
	gBFApp->mRenderDevice->mCurDrawLayer = drawLayer;
}

BF_EXPORT void BF_CALLTYPE DrawLayer_DrawToRenderTarget(DrawLayer* drawLayer, TextureSegment* textureSegment)
{
	RenderDevice* renderDevice = gBFApp->mRenderDevice;

	BP_ZONE("DrawLayer_DrawToRenderTarget DrawPart");
	RenderTarget* prevTarget = renderDevice->mCurRenderTarget;
	renderDevice->PhysSetRenderState(renderDevice->mDefaultRenderState);
	renderDevice->PhysSetRenderTarget(textureSegment->mTexture);
	drawLayer->Draw();
	drawLayer->Clear();
	renderDevice->mCurRenderTarget = prevTarget;
}
