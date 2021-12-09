#pragma once

#include "Common.h"
#include "Vector.h"

NS_BF_BEGIN

class Vector3;
class Matrix4;

class Sphere
{
public:
	Vector3 mCenter;
	float mRadius;

	Sphere();
	Sphere(const Sphere& X);
	Sphere(const Vector3& O);   // Point-Sphere
	Sphere(const Vector3& O, float R);   // Center and radius (not squared)
	Sphere(const Vector3& O, const Vector3& A);   // Sphere through two points
	Sphere(const Vector3& O, const Vector3& A, const Vector3& B);   // Sphere through three points
	Sphere(const Vector3& O, const Vector3& A, const Vector3& B, const Vector3& C);   // Sphere through four points

	Sphere& operator=(const Sphere& S);

	float GetDistance(const Vector3& P) const;  // Distance from p to boundary of the Sphere
	float GetDistanceSquare(const Vector3& P) const;  // Square distance from p to boundary of the Sphere

	static float GetDistance(const Sphere& S, const Vector3& P);  // Distance from p to boundary of the Sphere
	static float GetDistance(const Vector3& P, const Sphere& S);  // Distance from p to boundary of the Sphere

	static float GetDistanceSquare(const Sphere& S, const Vector3& P);  // Square distance from p to boundary of the Sphere
	static float GetDistanceSquare(const Vector3& P, const Sphere& S);  // Square distance from p to boundary of the Sphere

	static Sphere MiniBall(Vector3 P[], int p);   // Smallest enclosing sphere
	static Sphere SmallBall(Vector3 P[], int p);   // Enclosing sphere approximation

private:
	static Sphere RecurseMini(Vector3* P[], int p, int b = 0);
};

NS_BF_END
