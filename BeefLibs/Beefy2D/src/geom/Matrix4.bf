using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy.geom;

namespace Beefy.geom
{
    public struct Matrix4
    {
        public float m00;
        public float m01;
        public float m02;
        public float m03;
        public float m10;
        public float m11;
        public float m12;
        public float m13;
        public float m20;
        public float m21;
        public float m22;
        public float m23;
        public float m30;
        public float m31;
        public float m32;
        public float m33;

        public static readonly Matrix4 Identity = Matrix4(1f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f,
            0f, 0f, 1f, 0f,
            0f, 0f, 0f, 1f);

        public this(
		    float m00, float m01, float m02, float m03,
		    float m10, float m11, float m12, float m13,
		    float m20, float m21, float m22, float m23,
		    float m30, float m31, float m32, float m33)
	    {
		    this.m00 = m00;
		    this.m01 = m01;
		    this.m02 = m02;
		    this.m03 = m03;
		    this.m10 = m10;
		    this.m11 = m11;
		    this.m12 = m12;
		    this.m13 = m13;
		    this.m20 = m20;
		    this.m21 = m21;
		    this.m22 = m22;
		    this.m23 = m23;
		    this.m30 = m30;
		    this.m31 = m31;
		    this.m32 = m32;
		    this.m33 = m33;
	    }

        public static Matrix4 CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance)
        {
            Matrix4 matrix;
            if (nearPlaneDistance <= 0f)
            {
                Runtime.FatalError("nearPlaneDistance <= 0");
            }
            if (farPlaneDistance <= 0f)
            {
                Runtime.FatalError("farPlaneDistance <= 0");
            }
            if (nearPlaneDistance >= farPlaneDistance)
            {
                Runtime.FatalError("nearPlaneDistance >= farPlaneDistance");
            }
            /*matrix.M11 = (2f * nearPlaneDistance) / width;
            matrix.M12 = matrix.M13 = matrix.M14 = 0f;
            matrix.M22 = (2f * nearPlaneDistance) / height;
            matrix.M21 = matrix.M23 = matrix.M24 = 0f;
            matrix.M33 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
            matrix.M31 = matrix.M32 = 0f;
            matrix.M34 = -1f;
            matrix.M41 = matrix.M42 = matrix.M44 = 0f;
            matrix.M43 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);*/

            /*matrix.m00 = (2f * nearPlaneDistance) / width;
            matrix.m01 = 0f;                        
            matrix.m02 = 0f;
            matrix.m03 = 0f;

            matrix.m10 = 0f;
            matrix.m11 = (2f * nearPlaneDistance) / height;
            matrix.m12 = 0f;
            matrix.m13 = 0f;

            matrix.m20 = 0f;
            matrix.m21 = 0f;            
            matrix.m22 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
            matrix.m23 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);

            matrix.m30 = 0f;
            matrix.m31 = 0f;
            matrix.m32 = -1f;
            matrix.m33 = 0f;            */

            matrix.m00 = (2f * nearPlaneDistance) / width;
            matrix.m10 = matrix.m20 = matrix.m30 = 0f;
            matrix.m11 = (2f * nearPlaneDistance) / height;
            matrix.m01 = matrix.m21 = matrix.m31 = 0f;
            matrix.m22 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
            matrix.m02 = matrix.m12 = 0f;
            matrix.m32 = -1f;
            matrix.m03 = matrix.m13 = matrix.m33 = 0f;
            matrix.m23 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);

            return matrix;
        }

        public static Matrix4 CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance)
        {
            Matrix4 result;
            CreatePerspectiveFieldOfView(fieldOfView, aspectRatio, nearPlaneDistance, farPlaneDistance, out result);
            return result;
        }


        public static void CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance, out Matrix4 result)
        {
            if ((fieldOfView <= 0f) || (fieldOfView >= 3.141593f))
            {
                Runtime.FatalError("fieldOfView <= 0 or >= PI");
            }
            if (nearPlaneDistance <= 0f)
            {
                Runtime.FatalError("nearPlaneDistance <= 0");
            }
            if (farPlaneDistance <= 0f)
            {
                Runtime.FatalError("farPlaneDistance <= 0");
            }
            if (nearPlaneDistance >= farPlaneDistance)
            {
                Runtime.FatalError("nearPlaneDistance >= farPlaneDistance");
            }
            float num = 1f / ((float)Math.Tan((double)(fieldOfView * 0.5f)));
            float num9 = num / aspectRatio;
            result.m00 = num9;
            result.m10 = result.m20 = result.m30 = 0;
            result.m11 = num;
            result.m01 = result.m21 = result.m31 = 0;
            result.m02 = result.m12 = 0f;
            result.m22 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
            result.m32 = -1;
            result.m03 = result.m13 = result.m33 = 0;
            result.m23 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);
        }


        public static Matrix4 CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance, float farPlaneDistance)
        {
            Matrix4 result;
            CreatePerspectiveOffCenter(left, right, bottom, top, nearPlaneDistance, farPlaneDistance, out result);
            return result;
        }


        public static void CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance, float farPlaneDistance, out Matrix4 result)
        {
            if (nearPlaneDistance <= 0f)
            {
                Runtime.FatalError("nearPlaneDistance <= 0");
            }
            if (farPlaneDistance <= 0f)
            {
                Runtime.FatalError("farPlaneDistance <= 0");
            }
            if (nearPlaneDistance >= farPlaneDistance)
            {
                Runtime.FatalError("nearPlaneDistance >= farPlaneDistance");
            }
            result.m00 = (2f * nearPlaneDistance) / (right - left);
            result.m10 = result.m20 = result.m30 = 0;
            result.m11 = (2f * nearPlaneDistance) / (top - bottom);
            result.m01 = result.m21 = result.m31 = 0;
            result.m02 = (left + right) / (right - left);
            result.m12 = (top + bottom) / (top - bottom);
            result.m22 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
            result.m32 = -1;
            result.m23 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);
            result.m03 = result.m13 = result.m33 = 0;
        }

        public static Matrix4 Multiply(Matrix4 m1, Matrix4 m2)
	    {
		    Matrix4 r;
		    r.m00 = m1.m00 * m2.m00 + m1.m01 * m2.m10 + m1.m02 * m2.m20 + m1.m03 * m2.m30;
		    r.m01 = m1.m00 * m2.m01 + m1.m01 * m2.m11 + m1.m02 * m2.m21 + m1.m03 * m2.m31;
		    r.m02 = m1.m00 * m2.m02 + m1.m01 * m2.m12 + m1.m02 * m2.m22 + m1.m03 * m2.m32;
		    r.m03 = m1.m00 * m2.m03 + m1.m01 * m2.m13 + m1.m02 * m2.m23 + m1.m03 * m2.m33;

		    r.m10 = m1.m10 * m2.m00 + m1.m11 * m2.m10 + m1.m12 * m2.m20 + m1.m13 * m2.m30;
		    r.m11 = m1.m10 * m2.m01 + m1.m11 * m2.m11 + m1.m12 * m2.m21 + m1.m13 * m2.m31;
		    r.m12 = m1.m10 * m2.m02 + m1.m11 * m2.m12 + m1.m12 * m2.m22 + m1.m13 * m2.m32;
		    r.m13 = m1.m10 * m2.m03 + m1.m11 * m2.m13 + m1.m12 * m2.m23 + m1.m13 * m2.m33;

		    r.m20 = m1.m20 * m2.m00 + m1.m21 * m2.m10 + m1.m22 * m2.m20 + m1.m23 * m2.m30;
		    r.m21 = m1.m20 * m2.m01 + m1.m21 * m2.m11 + m1.m22 * m2.m21 + m1.m23 * m2.m31;
		    r.m22 = m1.m20 * m2.m02 + m1.m21 * m2.m12 + m1.m22 * m2.m22 + m1.m23 * m2.m32;
		    r.m23 = m1.m20 * m2.m03 + m1.m21 * m2.m13 + m1.m22 * m2.m23 + m1.m23 * m2.m33;

		    r.m30 = m1.m30 * m2.m00 + m1.m31 * m2.m10 + m1.m32 * m2.m20 + m1.m33 * m2.m30;
		    r.m31 = m1.m30 * m2.m01 + m1.m31 * m2.m11 + m1.m32 * m2.m21 + m1.m33 * m2.m31;
		    r.m32 = m1.m30 * m2.m02 + m1.m31 * m2.m12 + m1.m32 * m2.m22 + m1.m33 * m2.m32;
		    r.m33 = m1.m30 * m2.m03 + m1.m31 * m2.m13 + m1.m32 * m2.m23 + m1.m33 * m2.m33;

		    return r;
	    }

	    public static Matrix4 Transpose(Matrix4 m)
	    {
		    return Matrix4(
			    m.m00, m.m10, m.m20, m.m30,
			    m.m01, m.m11, m.m21, m.m31,
			    m.m02, m.m12, m.m22, m.m32,
			    m.m03, m.m13, m.m23, m.m33);
	    }

        public static Matrix4 CreateTranslation(float x, float y, float z)
	    {
		    return Matrix4(
			    1, 0, 0, x,
			    0, 1, 0, y,
			    0, 0, 1, z,
			    0, 0, 0, 1);
	    }

        public static Matrix4 CreateTransform(Vector3 position, Vector3 scale, Quaternion orientation)
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

        public static Matrix4 CreateRotationX(float radians)
        {
            Matrix4 result = Matrix4.Identity;

            var val1 = (float)Math.Cos(radians);
            var val2 = (float)Math.Sin(radians);

            result.m11 = val1;
            result.m21 = val2;
            result.m12 = -val2;
            result.m22 = val1;

            return result;
        }

        public static Matrix4 CreateRotationY(float radians)
        {
            Matrix4 returnMatrix = Matrix4.Identity;

            var val1 = (float)Math.Cos(radians);
            var val2 = (float)Math.Sin(radians);

            returnMatrix.m00 = val1;
            returnMatrix.m20 = -val2;
            returnMatrix.m02 = val2;
            returnMatrix.m22 = val1;

            return returnMatrix;
        }

        public static Matrix4 CreateRotationZ(float radians)
        {
            Matrix4 returnMatrix = Matrix4.Identity;

            var val1 = (float)Math.Cos(radians);
            var val2 = (float)Math.Sin(radians);

            returnMatrix.m00 = val1;
            returnMatrix.m10 = val2;
            returnMatrix.m01 = -val2;
            returnMatrix.m11 = val1;

            return returnMatrix;
        }

        public static Matrix4 CreateScale(float scale)
        {
            Matrix4 result;
            result.m00 = scale;
            result.m10 = 0;
            result.m20 = 0;
            result.m30 = 0;
            result.m01 = 0;
            result.m11 = scale;
            result.m21 = 0;
            result.m31 = 0;
            result.m02 = 0;
            result.m12 = 0;
            result.m22 = scale;
            result.m32 = 0;
            result.m03 = 0;
            result.m13 = 0;
            result.m23 = 0;
            result.m33 = 1;
            return result;
        }

        public static Matrix4 CreateScale(float xScale, float yScale, float zScale)
        {
            Matrix4 result;
            result.m00 = xScale;
            result.m10 = 0;
            result.m20 = 0;
            result.m30 = 0;
            result.m01 = 0;
            result.m11 = yScale;
            result.m21 = 0;
            result.m31 = 0;
            result.m02 = 0;
            result.m12 = 0;
            result.m22 = zScale;
            result.m32 = 0;
            result.m03 = 0;
            result.m13 = 0;
            result.m23 = 0;
            result.m33 = 1;
            return result;
        }

        public static Matrix4 CreateScale(Vector3 scales)
        {
            Matrix4 result;
            result.m00 = scales.mX;
            result.m10 = 0;
            result.m20 = 0;
            result.m30 = 0;
            result.m01 = 0;
            result.m11 = scales.mY;
            result.m21 = 0;
            result.m31 = 0;
            result.m02 = 0;
            result.m12 = 0;
            result.m22 = scales.mZ;
            result.m32 = 0;
            result.m03 = 0;
            result.m13 = 0;
            result.m23 = 0;
            result.m33 = 1;
            return result;
        }

        public static Matrix4 CreateTranslation(Vector3 position)
        {
            Matrix4 result;
            result.m00 = 1;
            result.m10 = 0;
            result.m20 = 0;
            result.m30 = 0;
            result.m01 = 0;
            result.m11 = 1;
            result.m21 = 0;
            result.m31 = 0;
            result.m02 = 0;
            result.m12 = 0;
            result.m22 = 1;
            result.m32 = 0;
            result.m03 = position.mX;
            result.m13 = position.mY;
            result.m23 = position.mZ;
            result.m33 = 1;
            return result;
        }

        /*public static Matrix4 Inverse()
        {
            Real m00 = m[0][0], m01 = m[0][1], m02 = m[0][2], m03 = m[0][3];
            Real m10 = m[1][0], m11 = m[1][1], m12 = m[1][2], m13 = m[1][3];
            Real m20 = m[2][0], m21 = m[2][1], m22 = m[2][2], m23 = m[2][3];
            Real m30 = m[3][0], m31 = m[3][1], m32 = m[3][2], m33 = m[3][3];

            Real v0 = m20 * m31 - m21 * m30;
            Real v1 = m20 * m32 - m22 * m30;
            Real v2 = m20 * m33 - m23 * m30;
            Real v3 = m21 * m32 - m22 * m31;
            Real v4 = m21 * m33 - m23 * m31;
            Real v5 = m22 * m33 - m23 * m32;

            Real t00 = + (v5 * m11 - v4 * m12 + v3 * m13);
            Real t10 = - (v5 * m10 - v2 * m12 + v1 * m13);
            Real t20 = + (v4 * m10 - v2 * m11 + v0 * m13);
            Real t30 = - (v3 * m10 - v1 * m11 + v0 * m12);

            Real invDet = 1 / (t00 * m00 + t10 * m01 + t20 * m02 + t30 * m03);

            Real d00 = t00 * invDet;
            Real d10 = t10 * invDet;
            Real d20 = t20 * invDet;
            Real d30 = t30 * invDet;

            Real d01 = - (v5 * m01 - v4 * m02 + v3 * m03) * invDet;
            Real d11 = + (v5 * m00 - v2 * m02 + v1 * m03) * invDet;
            Real d21 = - (v4 * m00 - v2 * m01 + v0 * m03) * invDet;
            Real d31 = + (v3 * m00 - v1 * m01 + v0 * m02) * invDet;

            v0 = m10 * m31 - m11 * m30;
            v1 = m10 * m32 - m12 * m30;
            v2 = m10 * m33 - m13 * m30;
            v3 = m11 * m32 - m12 * m31;
            v4 = m11 * m33 - m13 * m31;
            v5 = m12 * m33 - m13 * m32;

            Real d02 = + (v5 * m01 - v4 * m02 + v3 * m03) * invDet;
            Real d12 = - (v5 * m00 - v2 * m02 + v1 * m03) * invDet;
            Real d22 = + (v4 * m00 - v2 * m01 + v0 * m03) * invDet;
            Real d32 = - (v3 * m00 - v1 * m01 + v0 * m02) * invDet;

            v0 = m21 * m10 - m20 * m11;
            v1 = m22 * m10 - m20 * m12;
            v2 = m23 * m10 - m20 * m13;
            v3 = m22 * m11 - m21 * m12;
            v4 = m23 * m11 - m21 * m13;
            v5 = m23 * m12 - m22 * m13;

            Real d03 = - (v5 * m01 - v4 * m02 + v3 * m03) * invDet;
            Real d13 = + (v5 * m00 - v2 * m02 + v1 * m03) * invDet;
            Real d23 = - (v4 * m00 - v2 * m01 + v0 * m03) * invDet;
            Real d33 = + (v3 * m00 - v1 * m01 + v0 * m02) * invDet;

            return Matrix4(
                d00, d01, d02, d03,
                d10, d11, d12, d13,
                d20, d21, d22, d23,
                d30, d31, d32, d33);
        }*/
    
        bool IsAffine()
        {
            return m30 == 0 && m31 == 0 && m32 == 0 && m33 == 1;
        }

        public static Matrix4 InverseAffine(Matrix4 mtx)
        {
            Debug.Assert(mtx.IsAffine());

            float m10 = mtx.m10, m11 = mtx.m11, m12 = mtx.m12;
            float m20 = mtx.m20, m21 = mtx.m21, m22 = mtx.m22;

            float t00 = m22 * m11 - m21 * m12;
            float t10 = m20 * m12 - m22 * m10;
            float t20 = m21 * m10 - m20 * m11;

            float m00 = mtx.m00, m01 = mtx.m01, m02 = mtx.m02;

            float invDet = 1 / (m00 * t00 + m01 * t10 + m02 * t20);

            t00 *= invDet; t10 *= invDet; t20 *= invDet;

            m00 *= invDet; m01 *= invDet; m02 *= invDet;

            float r00 = t00;
            float r01 = m02 * m21 - m01 * m22;
            float r02 = m01 * m12 - m02 * m11;

            float r10 = t10;
            float r11 = m00 * m22 - m02 * m20;
            float r12 = m02 * m10 - m00 * m12;

            float r20 = t20;
            float r21 = m01 * m20 - m00 * m21;
            float r22 = m00 * m11 - m01 * m10;

            float m03 = mtx.m03, m13 = mtx.m13, m23 = mtx.m23;

            float r03 = -(r00 * m03 + r01 * m13 + r02 * m23);
            float r13 = -(r10 * m03 + r11 * m13 + r12 * m23);
            float r23 = -(r20 * m03 + r21 * m13 + r22 * m23);

            return Matrix4(
                r00, r01, r02, r03,
                r10, r11, r12, r13,
                r20, r21, r22, r23,
                  0, 0, 0, 1);
        }
    }
}
