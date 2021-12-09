#include "Sphere.h"
#include "Matrix4.h"

USING_NS_BF;

static const float radiusEpsilon = 1e-4f;   // NOTE: To avoid numerical inaccuracies

Sphere::Sphere()
{
	mRadius = -1;
}

Sphere::Sphere(const Sphere& S)
{
	mRadius = S.mRadius;
	mCenter = S.mCenter;
}

Sphere::Sphere(const Vector3& O)
{
	mRadius = 0 + radiusEpsilon;
	mCenter = O;
}

Sphere::Sphere(const Vector3& O, float R)
{
	mRadius = R;
	mCenter = O;
}

Sphere::Sphere(const Vector3& O, const Vector3& A)
{
	Vector3 a = A - O;

	Vector3 o = a * 0.5f;

	mRadius = o.GetMagnitude() + radiusEpsilon;
	mCenter = O + o;
}

Sphere::Sphere(const Vector3& O, const Vector3& A, const Vector3& B)
{
	Vector3 a = A - O;
	Vector3 b = B - O;

	Vector3 axb = Vector3::CrossProduct(a, b);

	float Denominator = 2.0f * Vector3::Dot(axb, axb);

	Vector3 o =
		((Vector3::CrossProduct(axb, a) * Vector3::Dot(b, b)) +	
		(Vector3::CrossProduct(b, axb) * Vector3::Dot(a, a))) / Denominator;

	mRadius = o.GetMagnitude() + radiusEpsilon;
	mCenter = O + o;
}

Sphere::Sphere(const Vector3& O, const Vector3& A, const Vector3& B, const Vector3& C)
{
	Vector3 a = A - O;
	Vector3 b = B - O;
	Vector3 c = C - O;

	float Denominator = 2.0f * Matrix4::Determinant(
		a.mX, a.mY, a.mZ,
		b.mX, b.mY, b.mZ,
		c.mX, c.mY, c.mZ);

	Vector3 o = (
		(Vector3::CrossProduct(a, b) * Vector3::Dot(c, c)) +
		(Vector3::CrossProduct(c, a) * Vector3::Dot(b, b)) +
		(Vector3::CrossProduct(b, c) * Vector3::Dot(a, a))) / Denominator;

	mRadius = o.GetMagnitude() + radiusEpsilon;
	mCenter = O + o;
}

Sphere& Sphere::operator=(const Sphere& S)
{
	mRadius = S.mRadius;
	mCenter = S.mCenter;

	return *this;
}

float Sphere::GetDistance(const Vector3& P) const
{
	return Vector3::GetDistance(P, mCenter) - mRadius;
}

float Sphere::GetDistanceSquare(const Vector3& P) const
{
	return Vector3::GetDistanceSquare(P, mCenter) - mRadius * mRadius;
}

float Sphere::GetDistance(const Sphere& S, const Vector3& P)
{
	return Vector3::GetDistance(P, S.mCenter) - S.mRadius;
}

float Sphere::GetDistance(const Vector3& P, const Sphere& S)
{
	return Vector3::GetDistance(P, S.mCenter) - S.mRadius;
}

float Sphere::GetDistanceSquare(const Sphere& S, const Vector3& P)
{
	return Vector3::GetDistanceSquare(P, S.mCenter) - S.mRadius * S.mRadius;
}

float Sphere::GetDistanceSquare(const Vector3& P, const Sphere& S)
{
	return Vector3::GetDistanceSquare(P, S.mCenter) - S.mRadius * S.mRadius;
}

Sphere Sphere::RecurseMini(Vector3* P[], int p, int b)
{
	Sphere MB;

	switch (b)
	{
	case 0:
		MB = Sphere();
		break;
	case 1:
		MB = Sphere(*P[-1]);
		break;
	case 2:
		MB = Sphere(*P[-1], *P[-2]);
		break;
	case 3:
		MB = Sphere(*P[-1], *P[-2], *P[-3]);
		break;
	case 4:
		MB = Sphere(*P[-1], *P[-2], *P[-3], *P[-4]);
		return MB;
	}

	for (int i = 0; i < p; i++)
	{
		if (MB.GetDistanceSquare(*P[i]) > 0)   // Signed square distance to sphere
		{
			for (int j = i; j > 0; j--)
			{
				Vector3* T = P[j];
				P[j] = P[j - 1];
				P[j - 1] = T;
			}

			MB = RecurseMini(P + 1, i, b + 1);
		}
	}

	return MB;
}

Sphere Sphere::MiniBall(Vector3 P[], int p)
{
	Vector3** L = new Vector3* [p];

	for (int i = 0; i < p; i++)
		L[i] = &P[i];

	Sphere MB = RecurseMini(L, p);

	delete[] L;

	return MB;
}

Sphere Sphere::SmallBall(Vector3 P[], int p)
{
	Vector3 mCenter;
	float mRadius = -1;

	if (p > 0)
	{		
		for (int i = 0; i < p; i++)
			mCenter += P[i];

		mCenter /= (float)p;

		for (int i = 0; i < p; i++)
		{
			float d2 = Vector3::GetDistanceSquare(P[i], mCenter);

			if (d2 > mRadius)
				mRadius = d2;
		}

		mRadius = sqrtf(mRadius) + radiusEpsilon;
	}

	return Sphere(mCenter, mRadius);
}
