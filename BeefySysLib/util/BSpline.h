#pragma once

#include "Common.h"
#include "Point.h"

NS_BF_BEGIN;

class BSpline2D
{
public:	
	Array<Point2D>			mInputPoints;
	int*					mUVals;

public:
	BSpline2D();
	~BSpline2D();

	void					AddPt(float x, float y);

	void					Calculate();
	void					Evaluate(float pct, float* x, float* y);
};

NS_BF_END;
