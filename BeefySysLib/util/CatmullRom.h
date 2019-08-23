#pragma once

#include "Common.h"
#include "Point.h"

NS_BF_BEGIN;

Point2D CatmullRomEvaluate(Point2D &p0, Point2D &p1, Point2D &p2, Point2D &p3, float tension, float t);

NS_BF_END;
