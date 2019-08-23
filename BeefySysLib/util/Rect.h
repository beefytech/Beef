#pragma once

#include "Common.h"

NS_BF_BEGIN;

class Rect
{
public:
	float mX;
	float mY;
	float mWidth;
	float mHeight;

public:
	Rect()
	{
		mX = 0;
		mY = 0;
		mWidth = 0;
		mHeight = 0;
	}

	bool operator!=(const Rect& r2)
	{
		return (mX != r2.mX) || (mY != r2.mY) || (mWidth != r2.mWidth) || (mHeight != r2.mHeight);
	}
};

NS_BF_END;