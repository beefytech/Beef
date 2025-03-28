#pragma once

#include "Common.h"

NS_BF_BEGIN;

class RenderTarget
{
public:
	int						mWidth;
	int						mHeight;
	int						mResizeNum;
	bool					mHasBeenTargeted;
	bool					mHasBeenDrawnTo;
	bool					mWantsClear;
	bool					mResetClear;

public:
	RenderTarget();
};


NS_BF_END;
