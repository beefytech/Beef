#pragma once

#include "Common.h"
#include "Point.h"

NS_BF_BEGIN;

PointF CatmullRomEvaluate(PointF &p0, PointF &p1, PointF &p2, PointF &p3, float tension, float t);

NS_BF_END;
