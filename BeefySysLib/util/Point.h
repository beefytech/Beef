#pragma once

#include "Common.h"

NS_BF_BEGIN;

template <typename T>
class Point
{
public:
	T x;
	T y;

public:
	Point(T x = 0, T y = 0)
	{
		this->x = x;
		this->y = y;
	}

	Point operator+(Point rhs)
	{
		return Point(x + rhs.x, y + rhs.y);
	}

	Point operator-(Point rhs)
	{
		return Point(x - rhs.x, y - rhs.y);
	}
};

typedef Point<double> PointD;
typedef Point<float> PointF;
typedef Point<int32> PointI32;

NS_BF_END;
