#pragma once

#include "Common.h"

NS_BF_BEGIN;

class Matrix4;
class Quaternion;

class TexCoords
{
public:
	float mU;
	float mV;

public:
	TexCoords()
	{
	}

	TexCoords(float u, float v) : mU(u), mV(v)
	{
	}

	static TexCoords FlipV(const TexCoords& texCoords)
	{
		return TexCoords(texCoords.mU, 1.0f - texCoords.mV);
	}
};

class Vector3
{
public:
	float mX;
	float mY;
	float mZ;

public:
	Vector3();
	Vector3(float x, float y, float z);

	float GetMagnitude() const;
	float GetMagnitudeSquare() const;
	static Vector3 Normalize(const Vector3& vec);
	static float Dot(const Vector3& vec1, const Vector3& vec2);
	static Vector3 CrossProduct(const Vector3& vec1, const Vector3& vec2);
	
	static float GetDistance(const Vector3& v0, const Vector3& v1)
	{
		return (v0 - v1).GetMagnitude();
	}

	static float GetDistanceSquare(const Vector3& v0, const Vector3& v1)
	{
		return (v0 - v1).GetMagnitudeSquare();
	}

	bool operator==(const Vector3& check) const
	{
		return (mX == check.mX) && (mY == check.mY) && (mZ == check.mZ);			
	}

	bool operator!=(const Vector3& check) const
	{
		return (mX != check.mX) || (mY != check.mY) || (mZ != check.mZ);
	}

	static Vector3 Transform(const Vector3& vec, const Matrix4& matrix);
	static Vector3 TransformW(const Vector3& vec, const Matrix4& matrix);
	static Vector3 Transform(const Vector3& vec, const Quaternion& quat);
	static Vector3 Transform2(const Vector3& vec, const Quaternion& quat);	

	static Vector3 Scale(const Vector3& vec, float scale)
	{
		return Vector3(vec.mX * scale, vec.mY * scale, vec.mZ * scale);
	}

	Vector3 operator +(const Vector3& v2) const
	{
		return Vector3(mX + v2.mX, mY + v2.mY, mZ + v2.mZ);
	}

	Vector3 operator *(const Vector3& v2) const
	{
		return Vector3(mX * v2.mX, mY * v2.mY, mZ * v2.mZ);
	}

	Vector3 operator *(float scale) const
	{
		return Vector3(mX * scale, mY * scale, mZ * scale);
	}

	Vector3 operator /(float scale) const
	{
		return Vector3(mX / scale, mY / scale, mZ / scale);
	}

	float operator^(const Vector3& v) // Angle between vectors
	{
		return acosf(Dot(*this / this->GetMagnitude(), v / v.GetMagnitude()));
	}
	
	inline Vector3& operator += (const Vector3& vec)
	{
		mX += vec.mX;
		mY += vec.mY;
		mZ += vec.mZ;
		return *this;
	}

	inline Vector3& operator -= (const Vector3& vec)
	{
		mX -= vec.mX;
		mY -= vec.mY;
		mZ -= vec.mZ;
		return *this;
	}

	inline Vector3& operator *= (float num)
	{
		mX *= num;
		mY *= num;
		mZ *= num;
		return *this;
	}

	inline Vector3& operator /= (float num)
	{
		mX /= num;
		mY /= num;
		mZ /= num;
		return *this;
	}

	inline Vector3 operator - (const Vector3& vec) const
	{
		Vector3 result;
		result.mX = mX - vec.mX;
		result.mY = mY - vec.mY;
		result.mZ = mZ - vec.mZ;
		return result;
	}

	inline Vector3 operator + (const Vector3& vec)
	{
		Vector3 result;
		result.mX = mX + vec.mX;
		result.mY = mY + vec.mY;
		result.mZ = mZ + vec.mZ;
		return result;
	}

	inline Vector3& operator *= (const Vector3& vec)
	{
		mX *= vec.mX;
		mY *= vec.mY;
		mZ *= vec.mZ;
		return *this;
	}
};

class Vector4
{
public:
	float mX;
	float mY;
	float mZ;
	float mW;

public:
	Vector4(float x = 0, float y = 0, float z = 0, float w = 0);
	
	bool operator==(const Vector4& check) const
	{
		return (mX == check.mX) && (mY == check.mY) && (mZ == check.mZ);
	}

	bool operator!=(const Vector4& check) const
	{
		return (mX != check.mX) || (mY != check.mY) || (mZ != check.mZ);
	}
	
	static Vector4 Scale(const Vector4& vec, float scale)
	{
		return Vector4(vec.mX * scale, vec.mY * scale, vec.mZ * scale, vec.mW * scale);
	}

	Vector4 operator +(const Vector4& v2) const
	{
		return Vector4(mX + v2.mX, mY + v2.mY, mZ + v2.mZ, mW + v2.mW);
	}

	Vector4 operator *(const Vector4& v2) const
	{
		return Vector4(mX * v2.mX, mY * v2.mY, mZ * v2.mZ, mW * v2.mW);
	}

	Vector4 operator *(float scale) const
	{
		return Vector4(mX * scale, mY * scale, mZ * scale, mW * scale);
	}

	inline Vector4& operator -= (const Vector4& vec)
	{
		mX -= vec.mX;
		mY -= vec.mY;
		mZ -= vec.mZ;
		mW -= vec.mW;
		return *this;
	}

	inline Vector4& operator *= (const Vector4& vec)
	{
		mX *= vec.mX;
		mY *= vec.mY;
		mZ *= vec.mZ;
		mW *= vec.mW;
		return *this;
	}
};

NS_BF_END;
