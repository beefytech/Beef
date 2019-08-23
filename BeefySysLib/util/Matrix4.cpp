#include "Matrix4.h"
#include "Quaternion.h"

USING_NS_BF;

Matrix4 Matrix4::sIdentity(
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f);

Matrix4 Matrix4::CreateTransform(const Vector3& position, const Vector3& scale, const Quaternion& orientation)
{
	// Ordering:
	//    1. Scale
	//    2. Rotate
	//    3. Translate

	Matrix4 rot = orientation.ToMatrix();
	return Matrix4(
		scale.mX * rot.m00, scale.mY * rot.m01, scale.mZ * rot.m02, position.mX,
		scale.mX * rot.m10, scale.mY * rot.m11, scale.mZ * rot.m12, position.mY,
		scale.mX * rot.m20, scale.mY * rot.m21, scale.mZ * rot.m22, position.mZ,	
		0, 0, 0, 1);
}
