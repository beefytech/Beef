#include "gfx/RenderCmd.h"
#include "gfx/RenderDevice.h"
#include "BFApp.h"

USING_NS_BF;

void RenderCmd::SetRenderState()
{
	RenderDevice* renderDevice = gBFApp->mRenderDevice;
	if (mRenderState != renderDevice->mPhysRenderState)
		renderDevice->PhysSetRenderState(mRenderState);
}

void RenderCmd::Free()
{
	if (mIsPoolHead)
		gBFApp->mRenderDevice->mPooledRenderCmdBuffers.FreeMemoryBlock(this);
}
