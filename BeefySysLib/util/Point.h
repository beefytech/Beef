#pragma once

#include "Common.h"

NS_BF_BEGIN;

class Point2D
{
public:
	float mX;
	float mY;

public:
	Point2D(float x = 0, float y = 0)
	{
		mX = x;
		mY = y;
	}
};

NS_BF_END;
