#pragma once

#include "Common.h"
#include "Point.h"
#include <vector>

NS_BF_BEGIN;

class CubicFuncSpline
{
public:	
	std::vector<Point2D>	mInputPoints;
	float* lagpoly;
	float* intpoly;
	float* slopes;
	
protected:
	void					Lagrange();
	void					ComputeSplineSlopes();

public:
	CubicFuncSpline();
	~CubicFuncSpline();

	void					AddPt(float x, float y);
	int						GetLength();	

	void					Calculate();
	float					Evaluate(float x);
	
};

class CubicUnitFuncSpline
{
public:	
	std::vector<float>		mInputPoints;
	float* lagpoly;
	float* intpoly;
	float* slopes;
	
protected:
	void					Lagrange();
	void					ComputeSplineSlopes();

public:
	CubicUnitFuncSpline();
	~CubicUnitFuncSpline();

	void					AddPt(float y);
	int						GetLength();	

	void					Calculate();
	float					Evaluate(float x);
	
};

NS_BF_END;
