#include "Vector.h"
#include "Matrix4.h"
#include "Quaternion.h"

USING_NS_BF;

Vector3::Vector3(float x, float y, float z)
{
	mX = x;
	mY = y;
	mZ = z;
}

float Vector3::GetMagnitude() const
{
	return sqrt(mX*mX + mY*mY + mZ*mZ);
}

Vector3 Vector3::Normalize(const Vector3& vec)
{
	float mag = vec.GetMagnitude();
	return Vector3(		
		vec.mX / mag,
		vec.mY / mag,
		vec.mZ / mag);
}

float Vector3::Dot(const Vector3& vec, const Vector3& vec2)
{
	return vec.mX*vec2.mX + vec.mY*vec2.mY + vec.mZ*vec2.mZ;
}

Vector3 Vector3::CrossProduct(const Vector3& vec1, const Vector3& vec2)
{
	return Vector3(
		vec1.mY * vec2.mZ - vec1.mZ * vec2.mY,
		vec1.mZ * vec2.mX - vec1.mX * vec2.mZ,
		vec1.mX * vec2.mY - vec1.mY * vec2.mX);
}

Vector3 Vector3::Transform(const Vector3& vec, const Matrix4& matrix)
{
	float fInvW = 1.0f / (matrix.m30 * vec.mX + matrix.m31 * vec.mY + matrix.m32 * vec.mZ + matrix.m33);

	return Vector3(
		(matrix.m00 * vec.mX + matrix.m01 * vec.mY + matrix.m02 * vec.mZ + matrix.m03) * fInvW,
		(matrix.m10 * vec.mX + matrix.m11 * vec.mY + matrix.m12 * vec.mZ + matrix.m13) * fInvW,
		(matrix.m20 * vec.mX + matrix.m21 * vec.mY + matrix.m22 * vec.mZ + matrix.m23) * fInvW);
}

Vector3 Vector3::Transform(const Vector3& vec, const Quaternion& quat)
{
	Vector3 result;

	Vector3 uv, uuv;
	Vector3 qvec(quat.mX, quat.mY, quat.mZ);
	uv = Vector3::CrossProduct(qvec, vec);
	uuv = Vector3::CrossProduct(qvec, uv);
	uv *= (2.0f * quat.mW);
	uuv *= 2.0f;

	return vec + uv + uuv;
}

Vector3 Vector3::Transform2(const Vector3& vec, const Quaternion& quat)
{
	Vector3 result;

	float x = 2 * (quat.mY * vec.mZ - quat.mZ * vec.mY);
	float y = 2 * (quat.mZ * vec.mX - quat.mX * vec.mZ);
	float z = 2 * (quat.mX * vec.mY - quat.mY * vec.mX);

	result.mX = vec.mX + x * quat.mW + (quat.mY * z - quat.mZ * y);
	result.mY = vec.mY + y * quat.mW + (quat.mZ * x - quat.mX * z);
	result.mZ = vec.mZ + z * quat.mW + (quat.mX * y - quat.mY * x);

	return result;
}