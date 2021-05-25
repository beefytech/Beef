#include "MathUtils.h"
#include "Sphere.h"

USING_NS_BF;

bool Beefy::RayIntersectsTriangle(const Vector3& rayOrigin, const Vector3& rayVector, const Vector3& vtx0, const Vector3& vtx1, const Vector3& vtx2, Vector3* outIntersectionPoint, float* outDistance)
{
	const float EPSILON = 0.0000001f;	
	Vector3 edge1, edge2, h, s, q;
	float a, f, u, v;
	edge1 = vtx1 - vtx0;
	edge2 = vtx2 - vtx0;
	
	h = Vector3::CrossProduct(rayVector, edge2);//rayVector.crossProduct(edge2);
	a = Vector3::Dot(edge1, h); //edge1.dotProduct(h);
	if (a > -EPSILON && a < EPSILON)
		return false;    // This ray is parallel to this triangle.
	f = 1.0f / a;
	s = rayOrigin - vtx0;
	u = f * Vector3::Dot(s, h); //s.dotProduct(h);
	if (u < 0.0 || u > 1.0)
		return false;
	q = Vector3::CrossProduct(s, edge1); //s.crossProduct(edge1);
	v = f * Vector3::Dot(rayVector, q); //rayVector.dotProduct(q);
	if (v < 0.0 || u + v > 1.0)
		return false;
	// At this stage we can compute t to find out where the intersection point is on the line.
	float distance = f * Vector3::Dot(edge2, q); //edge2.dotProduct(q);
	if (distance > EPSILON) // ray intersection
	{
		if (outIntersectionPoint != NULL)
			*outIntersectionPoint = rayOrigin + rayVector * distance;
		if (outDistance != NULL)
			*outDistance = distance;
		return true;
	}
	else // This means that there is a line intersection but not a ray intersection.
		return false;
}

bool Beefy::RayIntersectsCircle(const Vector3& rayOrigin, const Vector3& rayVector, const Sphere& sphere, Vector3* outIntersectionPoint, Vector3* outNormal, float* outDistance)
{
	Vector3 e = sphere.mCenter - rayOrigin;
	float rSq = sphere.mRadius * sphere.mRadius;

	float eSq = e.GetMagnitudeSquare();
	float a = Vector3::Dot(e, rayVector); // ray.direction is assumed to be normalized
	float bSq = /*sqrtf(*/eSq - (a * a)/*)*/;
	float f = sqrtf(fabsf((rSq)- /*(b * b)*/bSq));

	// Assume normal intersection!
	float t = a - f;

	// No collision has happened
	if (rSq - (eSq - a * a) < 0.0f) 
	{
		return false;
	}
	// Ray starts inside the sphere
	else if (eSq < rSq) 
	{
		// Just reverse direction
		t = a + f;
	}
		
	if (outDistance != NULL)
		*outDistance = t; 
	if ((outIntersectionPoint != NULL) || (outNormal != NULL))
	{
		Vector3 intersect = rayOrigin + rayVector * t;
		if (outIntersectionPoint != NULL)
			*outIntersectionPoint = intersect;
		if (outNormal != NULL)
			*outNormal = Vector3::Normalize(intersect - sphere.mCenter);
	}

	return true;
}

bool Beefy::RayIntersectsAABB(const Vector3& rayOrigin, const Vector3& rayVector, const AABB& aabb, Vector3* outIntersectionPoint, Vector3* outNormal, float* outDistance)
{
	const Vector3& min = aabb.mMin;
	const Vector3& max = aabb.mMax;

	// Any component of direction could be 0!
	// Address this by using a small number, close to
	// 0 in case any of directions components are 0
	float t1 = (min.mX - rayOrigin.mX) / ((fabs(rayVector.mX) < 0.00001f) ? 0.00001f : rayVector.mX);
	float t2 = (max.mX - rayOrigin.mX) / ((fabs(rayVector.mX) < 0.00001f) ? 0.00001f : rayVector.mX);
	float t3 = (min.mY - rayOrigin.mY) / ((fabs(rayVector.mY) < 0.00001f) ? 0.00001f : rayVector.mY);
	float t4 = (max.mY - rayOrigin.mY) / ((fabs(rayVector.mY) < 0.00001f) ? 0.00001f : rayVector.mY);
	float t5 = (min.mZ - rayOrigin.mZ) / ((fabs(rayVector.mZ) < 0.00001f) ? 0.00001f : rayVector.mZ);
	float t6 = (max.mZ - rayOrigin.mZ) / ((fabs(rayVector.mZ) < 0.00001f) ? 0.00001f : rayVector.mZ);

	float tmin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
	float tmax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));

	// if tmax < 0, ray is intersecting AABB
	// but entire AABB is being it's origin
	if (tmax < 0) {
		return false;
	}

	// if tmin > tmax, ray doesn't intersect AABB
	if (tmin > tmax) {
		return false;
	}

	float t_result = tmin;

	// If tmin is < 0, tmax is closer
	if (tmin < 0.0f) {
		t_result = tmax;
	}

	if ((outIntersectionPoint != NULL) || (outDistance != NULL))
	{
		Vector3 hitPoint = rayOrigin + rayVector * t_result;
		if (outIntersectionPoint != NULL)
			*outIntersectionPoint = hitPoint;
		if (outDistance != NULL)
			*outDistance = (rayOrigin - hitPoint).GetMagnitude();		
	}
	
	if (outNormal != NULL)
	{
		static Vector3 normals[] = {
			Vector3(-1, 0, 0),
			Vector3(1, 0, 0),
			Vector3(0, -1, 0),
			Vector3(0, 1, 0),
			Vector3(0, 0, -1),
			Vector3(0, 0, 1)
		};
		float t[] = { t1, t2, t3, t4, t5, t6 };

		for (int i = 0; i < 6; ++i) 
		{
			if (t_result == t[i])
				*outNormal = normals[i];
		}
	}

	return true;	
}
