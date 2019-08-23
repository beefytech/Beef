#pragma once

#include "Common.h"
#include "Matrix4.h"
#include <math.h>

NS_BF_BEGIN;

const float BF_MS_EPSILON = 1e-03f;

class Quaternion
{
public:
	float mX, mY, mZ, mW;

public:
	Quaternion()
	{}

	Quaternion(float x, float y, float z, float w) : 
		mX(x), mY(y), mZ(z), mW(w)
	{
	}

	Matrix4 ToMatrix2() const
	{
		// source -> http://content.gpwiki.org/index.php/OpenGL:Tutorials:Using_Quaternions_to_represent_rotation#Quaternion_to_Matrix
		float x2 = mX * mX;
		float y2 = mY * mY;
		float z2 = mZ * mZ;
		float xy = mX * mY;
		float xz = mX * mZ;
		float yz = mY * mZ;
		float wx = mW * mX;
		float wy = mW * mY;
		float wz = mW * mZ;

		// This calculation would be a lot more complicated for non-unit length quaternions
		// Note: The constructor of Matrix4 expects the Matrix in column-major format like expected by
		//   OpenGL
		return Matrix4(
			1.0f - 2.0f * (y2 + z2), 2.0f * (xy - wz), 2.0f * (xz + wy), 0.0f,		
			2.0f * (xy + wz), 1.0f - 2.0f * (x2 + z2), 2.0f * (yz - wx), 0.0f,
			2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (x2 + y2), 0.0f,
			2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (x2 + y2), 0.0f);		
	}

	Matrix4 ToMatrix() const
	{
		Matrix4 result;
		float fTx = mX + mX;
		float fTy = mY + mY;
		float fTz = mZ + mZ;
		float fTwx = fTx*mW;
		float fTwy = fTy*mW;
		float fTwz = fTz*mW;
		float fTxx = fTx*mX;
		float fTxy = fTy*mX;
		float fTxz = fTz*mX;
		float fTyy = fTy*mY;
		float fTyz = fTz*mY;
		float fTzz = fTz*mZ;

		result.m00 = 1.0f - (fTyy + fTzz);
		result.m01 = fTxy - fTwz;
		result.m02 = fTxz + fTwy;
		result.m03 = 0;

		result.m10 = fTxy + fTwz;
		result.m11 = 1.0f - (fTxx + fTzz);
		result.m12 = fTyz - fTwx;
		result.m13 = 0;

		result.m20 = fTxz - fTwy;
		result.m21 = fTyz + fTwx;
		result.m22 = 1.0f - (fTxx + fTyy);
		result.m23 = 0;

		result.m30 = 0;
		result.m31 = 0;
		result.m32 = 0;
		result.m33 = 1.0f;

		return result;
	}

	static float Dot(const Quaternion& rkP, const Quaternion& rkQ)
	{
		return rkP.mX*rkQ.mX + rkP.mY*rkQ.mY + rkP.mZ*rkQ.mZ + rkP.mW*rkQ.mW;
	}

	Quaternion operator- () const
	{
		return Quaternion(-mX, -mY, -mZ, -mW);
	}

	Quaternion operator* (float fScalar) const
	{
		return Quaternion(fScalar*mX, fScalar*mY, fScalar*mZ, fScalar*mW);
	}

	Quaternion operator+ (const Quaternion& rkQ) const
	{
		return Quaternion(mX + rkQ.mX, mY + rkQ.mY, mZ + rkQ.mZ, mW + rkQ.mW);
	}

	static Quaternion Slerp(float fT, const Quaternion& rkP, const Quaternion& rkQ, bool shortestPath);

	float Norm() const
	{
		return mX*mX + mY*mY + mZ*mZ + mW*mW;
	}

	static Quaternion Normalise(const Quaternion& quat)
	{
		float len = quat.Norm();
		float factor = 1.0f / sqrt(len);
		return quat * factor;		
	}
};

Quaternion operator*(float fScalar, const Quaternion& rkQ);

NS_BF_END;
