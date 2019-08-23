#pragma once

#include "Common.h"
#include "Point.h"
#include <vector>

NS_BF_BEGIN;

class CubicVal
{
public:
	float a,b,c,d;         /* a + b*u + c*u^2 +d*u^3 */

public:
	CubicVal(float a = 0, float b = 0, float c = 0, float d = 0)
	{
		this->a = a;
		this->b = b;
		this->c = c;
		this->d = d;
	}

	void Set(float a, float b, float c, float d)
	{
		this->a = a;
		this->b = b;
		this->c = c;
		this->d = d;
	}
  
	/** evaluate cubic */
	float Evaluate(float u) 
	{
		return (((d*u) + c)*u + b)*u + a;
	}
};

class CubicSpline2D
{
public:	
	std::vector<Point2D>	mInputPoints;
	CubicVal*				mXCubicArray;
	CubicVal*				mYCubicArray;

public:
	CubicVal*				SolveCubic(std::vector<float> vals);

public:
	CubicSpline2D();
	~CubicSpline2D();

	void					AddPt(float x, float y);
	int						GetLength();

	void					Calculate();
	Point2D					Evaluate(float t);
};

NS_BF_END;
