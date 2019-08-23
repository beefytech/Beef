#pragma once

#include "Common.h"
#include "Point.h"
#include <vector>

NS_BF_BEGIN;

class PolySpline2D
{
public:	
	std::vector<Point2D>	mInputPoints;
	

public:
	float*					mCoefs;	

public:
	PolySpline2D();
	~PolySpline2D();

	void					AddPt(float x, float y);
	int						GetLength();

	void					Calculate();
	float					Evaluate(float x);
};


NS_BF_END;
