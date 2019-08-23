#pragma once

#include "Common.h"

NS_BF_BEGIN;

class RenderState;
class RenderDevice;
class RenderWindow;
class DrawLayer;

class RenderCmd
{
public:
	RenderCmd* mNext;
	RenderState* mRenderState;
	bool mIsPoolHead;
	int mCmdIdx;

public:
	RenderCmd()
	{
		mNext = NULL;
		mRenderState = NULL;
		mIsPoolHead = false;
		mCmdIdx = -1;
	}

	void SetRenderState();
	virtual void CommandQueued(DrawLayer* drawLayer) {}
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) = 0;
	virtual void Free();
};

NS_BF_END;
