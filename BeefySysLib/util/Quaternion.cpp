#include "Quaternion.h"

USING_NS_BF;

Quaternion Beefy::operator* (float fScalar, const Quaternion& rkQ)
{
	return Quaternion(fScalar*rkQ.mX, fScalar*rkQ.mY, fScalar*rkQ.mZ, fScalar*rkQ.mW);
}

Beefy::Quaternion Beefy::Quaternion::Slerp(float fT, const Quaternion& rkP, const Quaternion& rkQ, bool shortestPath)
{
	float fCos = Dot(rkP, rkQ);
	Quaternion rkT;

	// Do we need to invert rotation?
	if (fCos < 0.0f && shortestPath)
	{
		fCos = -fCos;
		rkT = -rkQ;
	}
	else
	{
		rkT = rkQ;
	}

	if ((fabs(fCos) < 1.0f - BF_MS_EPSILON) && (false))
	{
		// Standard case (slerp)
		float fSin = sqrt(1.0f - (fCos * fCos));
		float fAngle = atan2(fSin, fCos);
		float fInvSin = 1.0f / fSin;
		float fCoeff0 = sin((1.0f - fT) * fAngle) * fInvSin;
		float fCoeff1 = sin(fT * fAngle) * fInvSin;
		return fCoeff0 * rkP + fCoeff1 * rkT;
	}
	else
	{
		// There are two situations:
		// 1. "rkP" and "rkQ" are very close (fCos ~= +1), so we can do a linear
		//    interpolation safely.
		// 2. "rkP" and "rkQ" are almost inverse of each other (fCos ~= -1), there
		//    are an infinite number of possibilities interpolation. but we haven't
		//    have method to fix this case, so just use linear interpolation here.
		Quaternion t = (1.0f - fT) * rkP + fT * rkT;
		// taking the complement requires renormalisation
		t = Normalise(t);

		//OutputDebugStrF("%0.3f %0.3f %0.3f %0.3f\n", t.mX, t.mY, t.mZ, t.mW);
		return t;

		//return (fT < 0.5f) ? rkP : rkQ;
	}
}
