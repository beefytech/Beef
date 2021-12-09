#pragma once

#include "Vector.h"
#include "Matrix4.h"

NS_BF_BEGIN

class AABB
{
public:
	Vector3 mMin;
	Vector3 mMax;
};

class Sphere;

bool RayIntersectsTriangle(const Vector3& rayOrigin, const Vector3& rayVector, const Vector3& vtx0, const Vector3& vtx1, const Vector3& vtx2, Vector3* outIntersectionPoint, float* distance);
bool RayIntersectsCircle(const Vector3& rayOrigin, const Vector3& rayVector, const Sphere& sphere, Vector3* outIntersectionPoint, Vector3* outNormal, float* outDistance);
bool RayIntersectsAABB(const Vector3& rayOrigin, const Vector3& rayVector, const AABB& aabb, Vector3* outIntersectionPoint, Vector3* outNormal, float* outDistance);

NS_BF_END