using System;
using System.Diagnostics;
using System.Collections;
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

		public static Matrix4 CreateFromColumnMajor(
			float m00, float m10, float m20, float m30,
			float m01, float m11, float m21, float m31,
			float m02, float m12, float m22, float m32,
			float m03, float m13, float m23, float m33)
		{
			return .(
				m00, m01, m02, m03,
				m10, m11, m12, m13,
				m20, m21, m22, m23,
				m30, m31, m32, m33);
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

			r.m00 = (((m1.m00 * m2.m00) + (m1.m10 * m2.m01)) + (m1.m20 * m2.m02)) + (m1.m30 * m2.m03);
			r.m10 = (((m1.m00 * m2.m10) + (m1.m10 * m2.m11)) + (m1.m20 * m2.m12)) + (m1.m30 * m2.m13);
			r.m20 = (((m1.m00 * m2.m20) + (m1.m10 * m2.m21)) + (m1.m20 * m2.m22)) + (m1.m30 * m2.m23);
			r.m30 = (((m1.m00 * m2.m30) + (m1.m10 * m2.m31)) + (m1.m20 * m2.m32)) + (m1.m30 * m2.m33);
			r.m01 = (((m1.m01 * m2.m00) + (m1.m11 * m2.m01)) + (m1.m21 * m2.m02)) + (m1.m31 * m2.m03);
			r.m11 = (((m1.m01 * m2.m10) + (m1.m11 * m2.m11)) + (m1.m21 * m2.m12)) + (m1.m31 * m2.m13);
			r.m21 = (((m1.m01 * m2.m20) + (m1.m11 * m2.m21)) + (m1.m21 * m2.m22)) + (m1.m31 * m2.m23);
			r.m31 = (((m1.m01 * m2.m30) + (m1.m11 * m2.m31)) + (m1.m21 * m2.m32)) + (m1.m31 * m2.m33);
			r.m02 = (((m1.m02 * m2.m00) + (m1.m12 * m2.m01)) + (m1.m22 * m2.m02)) + (m1.m32 * m2.m03);
			r.m12 = (((m1.m02 * m2.m10) + (m1.m12 * m2.m11)) + (m1.m22 * m2.m12)) + (m1.m32 * m2.m13);
			r.m22 = (((m1.m02 * m2.m20) + (m1.m12 * m2.m21)) + (m1.m22 * m2.m22)) + (m1.m32 * m2.m23);
			r.m32 = (((m1.m02 * m2.m30) + (m1.m12 * m2.m31)) + (m1.m22 * m2.m32)) + (m1.m32 * m2.m33);
			r.m03 = (((m1.m03 * m2.m00) + (m1.m13 * m2.m01)) + (m1.m23 * m2.m02)) + (m1.m33 * m2.m03);
			r.m13 = (((m1.m03 * m2.m10) + (m1.m13 * m2.m11)) + (m1.m23 * m2.m12)) + (m1.m33 * m2.m13);
			r.m23 = (((m1.m03 * m2.m20) + (m1.m13 * m2.m21)) + (m1.m23 * m2.m22)) + (m1.m33 * m2.m23);
			r.m33 = (((m1.m03 * m2.m30) + (m1.m13 * m2.m31)) + (m1.m23 * m2.m32)) + (m1.m33 * m2.m33);

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

		public static void Invert(Matrix4 matrix, out Matrix4 result)
		{
			float num1 = matrix.m00;
			float num2 = matrix.m10;
			float num3 = matrix.m20;
			float num4 = matrix.m30;
			float num5 = matrix.m01;
			float num6 = matrix.m11;
			float num7 = matrix.m21;
			float num8 = matrix.m31;
			float num9 =  matrix.m02;
			float num10 = matrix.m12;
			float num11 = matrix.m22;
			float num12 = matrix.m32;
			float num13 = matrix.m03;
			float num14 = matrix.m13;
			float num15 = matrix.m23;
			float num16 = matrix.m33;
			float num17 = (float) ((double) num11 * (double) num16 - (double) num12 * (double) num15);
			float num18 = (float) ((double) num10 * (double) num16 - (double) num12 * (double) num14);
			float num19 = (float) ((double) num10 * (double) num15 - (double) num11 * (double) num14);
			float num20 = (float) ((double) num9 * (double) num16 - (double) num12 * (double) num13);
			float num21 = (float) ((double) num9 * (double) num15 - (double) num11 * (double) num13);
			float num22 = (float) ((double) num9 * (double) num14 - (double) num10 * (double) num13);
			float num23 = (float) ((double) num6 * (double) num17 - (double) num7 * (double) num18 + (double) num8 * (double) num19);
			float num24 = (float) -((double) num5 * (double) num17 - (double) num7 * (double) num20 + (double) num8 * (double) num21);
			float num25 = (float) ((double) num5 * (double) num18 - (double) num6 * (double) num20 + (double) num8 * (double) num22);
			float num26 = (float) -((double) num5 * (double) num19 - (double) num6 * (double) num21 + (double) num7 * (double) num22);
			float num27 = (float) (1.0 / ((double) num1 * (double) num23 + (double) num2 * (double) num24 + (double) num3 * (double) num25 + (double) num4 * (double) num26));
			
			result.m00 = num23 * num27;
			result.m01 = num24 * num27;
			result.m02 = num25 * num27;
			result.m03 = num26 * num27;
			result.m10 = (float)(-((double) num2 * (double) num17 - (double) num3 * (double) num18 + (double) num4 * (double) num19) * num27);
			result.m11 = (float) ((double) num1 * (double) num17 - (double) num3 * (double) num20 + (double) num4 * (double) num21) * num27;
			result.m12 = (float)(-((double) num1 * (double) num18 - (double) num2 * (double) num20 + (double) num4 * (double) num22) * num27);
			result.m13 = (float) ((double) num1 * (double) num19 - (double) num2 * (double) num21 + (double) num3 * (double) num22) * num27;
			float num28 = (float) ((double) num7 * (double) num16 - (double) num8 * (double) num15);
			float num29 = (float) ((double) num6 * (double) num16 - (double) num8 * (double) num14);
			float num30 = (float) ((double) num6 * (double) num15 - (double) num7 * (double) num14);
			float num31 = (float) ((double) num5 * (double) num16 - (double) num8 * (double) num13);
			float num32 = (float) ((double) num5 * (double) num15 - (double) num7 * (double) num13);
			float num33 = (float) ((double) num5 * (double) num14 - (double) num6 * (double) num13);
			result.m20 = (float) ((double) num2 * (double) num28 - (double) num3 * (double) num29 + (double) num4 * (double) num30) * num27;
			result.m21 = (float)(-((double) num1 * (double) num28 - (double) num3 * (double) num31 + (double) num4 * (double) num32) * num27);
			result.m22 = (float) ((double) num1 * (double) num29 - (double) num2 * (double) num31 + (double) num4 * (double) num33) * num27;
			result.m23 = (float)(-((double) num1 * (double) num30 - (double) num2 * (double) num32 + (double) num3 * (double) num33) * num27);
			float num34 = (float) ((double) num7 * (double) num12 - (double) num8 * (double) num11);
			float num35 = (float) ((double) num6 * (double) num12 - (double) num8 * (double) num10);
			float num36 = (float) ((double) num6 * (double) num11 - (double) num7 * (double) num10);
			float num37 = (float) ((double) num5 * (double) num12 - (double) num8 * (double) num9);
			float num38 = (float) ((double) num5 * (double) num11 - (double) num7 * (double) num9);
			float num39 = (float) ((double) num5 * (double) num10 - (double) num6 * (double) num9);
			result.m30 = (float)(-((double) num2 * (double) num34 - (double) num3 * (double) num35 + (double) num4 * (double) num36) * num27);
			result.m31 = (float) ((double) num1 * (double) num34 - (double) num3 * (double) num37 + (double) num4 * (double) num38) * num27;
			result.m32 = (float)(-((double) num1 * (double) num35 - (double) num2 * (double) num37 + (double) num4 * (double) num39) * num27);
			result.m33 = (float) ((double) num1 * (double) num36 - (double) num2 * (double) num38 + (double) num3 * (double) num39) * num27;
			
			/*
			
			
		    ///
		    // Use Laplace expansion theorem to calculate the inverse of a 4x4 matrix
		    // 
		    // 1. Calculate the 2x2 determinants needed the 4x4 determinant based on the 2x2 determinants 
		    // 3. Create the adjugate matrix, which satisfies: A * adj(A) = det(A) * I
		    // 4. Divide adjugate matrix with the determinant to find the inverse
		    
		    float det1, det2, det3, det4, det5, det6, det7, det8, det9, det10, det11, det12;
		    float detMatrix;
		    FindDeterminants(ref matrix, out detMatrix, out det1, out det2, out det3, out det4, out det5, out det6, 
		                     out det7, out det8, out det9, out det10, out det11, out det12);
		    
		    float invDetMatrix = 1f / detMatrix;
		    
		    Matrix ret; // Allow for matrix and result to point to the same structure
		    
		    ret.M11 = (matrix.M22*det12 - matrix.M23*det11 + matrix.M24*det10) * invDetMatrix;
		    ret.M12 = (-matrix.M12*det12 + matrix.M13*det11 - matrix.M14*det10) * invDetMatrix;
		    ret.M13 = (matrix.M42*det6 - matrix.M43*det5 + matrix.M44*det4) * invDetMatrix;
		    ret.M14 = (-matrix.M32*det6 + matrix.M33*det5 - matrix.M34*det4) * invDetMatrix;
		    ret.M21 = (-matrix.M21*det12 + matrix.M23*det9 - matrix.M24*det8) * invDetMatrix;
		    ret.M22 = (matrix.M11*det12 - matrix.M13*det9 + matrix.M14*det8) * invDetMatrix;
		    ret.M23 = (-matrix.M41*det6 + matrix.M43*det3 - matrix.M44*det2) * invDetMatrix;
		    ret.M24 = (matrix.M31*det6 - matrix.M33*det3 + matrix.M34*det2) * invDetMatrix;
		    ret.M31 = (matrix.M21*det11 - matrix.M22*det9 + matrix.M24*det7) * invDetMatrix;
		    ret.M32 = (-matrix.M11*det11 + matrix.M12*det9 - matrix.M14*det7) * invDetMatrix;
		    ret.M33 = (matrix.M41*det5 - matrix.M42*det3 + matrix.M44*det1) * invDetMatrix;
		    ret.M34 = (-matrix.M31*det5 + matrix.M32*det3 - matrix.M34*det1) * invDetMatrix;
		    ret.M41 = (-matrix.M21*det10 + matrix.M22*det8 - matrix.M23*det7) * invDetMatrix;
		    ret.M42 = (matrix.M11*det10 - matrix.M12*det8 + matrix.M13*det7) * invDetMatrix;
		    ret.M43 = (-matrix.M41*det4 + matrix.M42*det2 - matrix.M43*det1) * invDetMatrix;
		    ret.M44 = (matrix.M31*det4 - matrix.M32*det2 + matrix.M33*det1) * invDetMatrix;
		    
		    result = ret;
		    */
		}

		public static Matrix4 Invert(Matrix4 matrix)
		{
		    Invert(matrix, var outMatrix);
		    return outMatrix;
		}

		/// <summary>
		/// Decomposes this matrix to translation, rotation and scale elements. Returns <c>true</c> if matrix can be decomposed; <c>false</c> otherwise.
		/// </summary>
		/// <param name="scale">Scale vector as an output parameter.
		/// <param name="rotation">Rotation quaternion as an output parameter.
		/// <param name="translation">Translation vector as an output parameter.
		/// <returns><c>true</c> if matrix can be decomposed; <c>false</c> otherwise.</returns>
		public bool Decompose(
			out Vector3 scale,
			out Quaternion rotation,
			out Vector3 translation
		) {
			translation.mX = m03;
			translation.mY = m13;
			translation.mZ = m23;

			float xs = (Math.Sign(m00 * m10 * m20 * m30) < 0) ? -1 : 1;
			float ys = (Math.Sign(m01 * m11 * m21 * m31) < 0) ? -1 : 1;
			float zs = (Math.Sign(m02 * m12 * m22 * m32) < 0) ? -1 : 1;

			scale.mX = xs * (float) Math.Sqrt(m00 * m00 + m10 * m10 + m20 * m20);
			scale.mY = ys * (float) Math.Sqrt(m01 * m01 + m11 * m11 + m21 * m21);
			scale.mZ = zs * (float) Math.Sqrt(m02 * m02 + m12 * m12 + m22 * m22);

			if (Math.WithinEpsilon(scale.mX, 0.0f) ||
				Math.WithinEpsilon(scale.mY, 0.0f) ||
				Math.WithinEpsilon(scale.mZ, 0.0f)	)
			{
				rotation = Quaternion.Identity;
				return false;
			}

			Matrix4 m1 = Matrix4.CreateFromColumnMajor(
				m00 / scale.mX, m10 / scale.mX, m20 / scale.mX, 0,
				m01 / scale.mY, m11 / scale.mY, m21 / scale.mY, 0,
				m02 / scale.mZ, m12 / scale.mZ, m22 / scale.mZ, 0,
				0, 0, 0, 1
			);

			rotation = Quaternion.CreateFromRotationMatrix(m1);
			return true;
		}

		/// <summary>
		/// Returns a determinant of this matrix.
		/// </summary>
		/// <returns>Determinant of this matrix</returns>
		/// <remarks>See more about determinant here - http://en.wikipedia.org/wiki/Determinant.
		/// </remarks>
		public float Determinant()
		{
			float num18 = (m22 * m33) - (m32 * m23);
			float num17 = (m12 * m33) - (m32 * m13);
			float num16 = (m12 * m23) - (m22 * m13);
			float num15 = (m02 * m33) - (m32 * m03);
			float num14 = (m02 * m23) - (m22 * m03);
			float num13 = (m02 * m13) - (m12 * m03);
			return (
				(
					(
						(m00 * (((m11 * num18) - (m21 * num17)) + (m31 * num16))) -
						(m10 * (((m01 * num18) - (m21 * num15)) + (m31 * num14)))
					) + (m20 * (((m01 * num17) - (m11 * num15)) + (m31 * num13)))
				) - (m30 * (((m01 * num16) - (m11 * num14)) + (m21 * num13)))
			);
		}

		/// <summary>
		/// Creates a new matrix for spherical billboarding that rotates around specified object position.
		/// </summary>
		/// <param name="objectPosition">Position of billboard object. It will rotate around that vector.
		/// <param name="cameraPosition">The camera position.
		/// <param name="cameraUpVector">The camera up vector.
		/// <param name="cameraForwardVector">Optional camera forward vector.
		/// <returns>The matrix for spherical billboarding.</returns>
		public static Matrix4 CreateBillboard(
			Vector3 objectPosition,
			Vector3 cameraPosition,
			Vector3 cameraUpVector,
			Nullable<Vector3> cameraForwardVector
		) {
			Matrix4 result;

			// Delegate to the other overload of the function to do the work
			CreateBillboard(
				objectPosition,
				cameraPosition,
				cameraUpVector,
				cameraForwardVector,
				out result
			);

			return result;
		}

		/// <summary>
		/// Creates a new matrix for spherical billboarding that rotates around specified object position.
		/// </summary>
		/// <param name="objectPosition">Position of billboard object. It will rotate around that vector.
		/// <param name="cameraPosition">The camera position.
		/// <param name="cameraUpVector">The camera up vector.
		/// <param name="cameraForwardVector">Optional camera forward vector.
		/// <param name="result">The matrix for spherical billboarding as an output parameter.
		public static void CreateBillboard(
			Vector3 objectPosition,
			Vector3 cameraPosition,
			Vector3 cameraUpVector,
			Vector3? cameraForwardVector,
			out Matrix4 result
		) {
			Vector3 vector;
			Vector3 vector2;
			Vector3 vector3;
			vector.mX = objectPosition.mX - cameraPosition.mX;
			vector.mY = objectPosition.mY - cameraPosition.mY;
			vector.mZ = objectPosition.mZ - cameraPosition.mZ;
			float num = vector.LengthSquared;
			if (num < 0.0001f)
			{
				vector = cameraForwardVector.HasValue ?
					-cameraForwardVector.Value :
					Vector3.Forward;
			}
			else
			{
				vector *= (float) (1f / ((float) Math.Sqrt((double) num)));
			}
			vector3 = Vector3.Cross(cameraUpVector, vector);
			vector3.Normalize();
			vector2 = Vector3.Cross(vector, vector3);
			result.m00 = vector3.mX;
			result.m10 = vector3.mY;
			result.m20 = vector3.mZ;
			result.m30 = 0;
			result.m01 = vector2.mX;
			result.m11 = vector2.mY;
			result.m21 = vector2.mZ;
			result.m31 = 0;
			result.m02 = vector.mX;
			result.m12 = vector.mY;
			result.m22 = vector.mZ;
			result.m32 = 0;
			result.m03 = objectPosition.mX;
			result.m13 = objectPosition.mY;
			result.m23 = objectPosition.mZ;
			result.m33 = 1;
		}

		/// <summary>
		/// Creates a new matrix for cylindrical billboarding that rotates around specified axis.
		/// </summary>
		/// <param name="objectPosition">Object position the billboard will rotate around.
		/// <param name="cameraPosition">Camera position.
		/// <param name="rotateAxis">Axis of billboard for rotation.
		/// <param name="cameraForwardVector">Optional camera forward vector.
		/// <param name="objectForwardVector">Optional object forward vector.
		/// <returns>The matrix for cylindrical billboarding.</returns>
		public static Matrix4 CreateConstrainedBillboard(
			Vector3 objectPosition,
			Vector3 cameraPosition,
			Vector3 rotateAxis,
			Nullable<Vector3> cameraForwardVector,
			Nullable<Vector3> objectForwardVector
		) {
			Matrix4 result;
			CreateConstrainedBillboard(
				objectPosition,
				cameraPosition,
				rotateAxis,
				cameraForwardVector,
				objectForwardVector,
				out result
			);
			return result;
		}

		/// <summary>
		/// Creates a new matrix for cylindrical billboarding that rotates around specified axis.
		/// </summary>
		/// <param name="objectPosition">Object position the billboard will rotate around.
		/// <param name="cameraPosition">Camera position.
		/// <param name="rotateAxis">Axis of billboard for rotation.
		/// <param name="cameraForwardVector">Optional camera forward vector.
		/// <param name="objectForwardVector">Optional object forward vector.
		/// <param name="result">The matrix for cylindrical billboarding as an output parameter.
		public static void CreateConstrainedBillboard(
			Vector3 objectPosition,
			Vector3 cameraPosition,
			Vector3 rotateAxis,
			Vector3? cameraForwardVector,
			Vector3? objectForwardVector,
			out Matrix4 result
		) {
			float num;
			Vector3 vector;
			Vector3 vector2;
			Vector3 vector3;
			vector2.mX = objectPosition.mX - cameraPosition.mX;
			vector2.mY = objectPosition.mY - cameraPosition.mY;
			vector2.mZ = objectPosition.mZ - cameraPosition.mZ;
			float num2 = vector2.LengthSquared;
			if (num2 < 0.0001f)
			{
				vector2 = cameraForwardVector.HasValue ?
					-cameraForwardVector.Value :
					Vector3.Forward;
			}
			else
			{
				vector2 *= (float) (1f / ((float) Math.Sqrt((double) num2)));
			}
			Vector3 vector4 = rotateAxis;
			num = Vector3.Dot(rotateAxis, vector2);
			if (Math.Abs(num) > 0.9982547f)
			{
				if (objectForwardVector.HasValue)
				{
					vector = objectForwardVector.Value;
					num = Vector3.Dot(rotateAxis, vector);
					if (Math.Abs(num) > 0.9982547f)
					{
						num = (
							(rotateAxis.mX * Vector3.Forward.mX) +
							(rotateAxis.mY * Vector3.Forward.mY)
						) + (rotateAxis.mZ * Vector3.Forward.mZ);
						vector = (Math.Abs(num) > 0.9982547f) ?
							Vector3.Right :
							Vector3.Forward;
					}
				}
				else
				{
					num = (
						(rotateAxis.mX * Vector3.Forward.mX) +
						(rotateAxis.mY * Vector3.Forward.mY)
					) + (rotateAxis.mZ * Vector3.Forward.mZ);
					vector = (Math.Abs(num) > 0.9982547f) ?
						Vector3.Right :
						Vector3.Forward;
				}
				vector3 = Vector3.Cross(rotateAxis, vector);
				vector3.Normalize();
				vector = Vector3.Cross(vector3, rotateAxis);
				vector.Normalize();
			}
			else
			{
				vector3 = Vector3.Cross(rotateAxis, vector2);
				vector3.Normalize();
				vector = Vector3.Cross(vector3, vector4);
				vector.Normalize();
			}

			result.m00 = vector3.mX;
			result.m10 = vector3.mY;
			result.m20 = vector3.mZ;
			result.m30 = 0;
			result.m01 = vector4.mX;
			result.m11 = vector4.mY;
			result.m21 = vector4.mZ;
			result.m31 = 0;
			result.m02 = vector.mX;
			result.m12 = vector.mY;
			result.m22 = vector.mZ;
			result.m32 = 0;
			result.m03 = objectPosition.mX;
			result.m13 = objectPosition.mY;
			result.m23 = objectPosition.mZ;
			result.m33 = 1;
		}

		/// <summary>
		/// Creates a new matrix which contains the rotation moment around specified axis.
		/// </summary>
		/// <param name="axis">The axis of rotation.
		/// <param name="angle">The angle of rotation in radians.
		/// <returns>The rotation matrix.</returns>
		public static Matrix4 CreateFromAxisAngle(Vector3 axis, float angle)
		{
			Matrix4 result;
			CreateFromAxisAngle(axis, angle, out result);
			return result;
		}

		/// <summary>
		/// Creates a new matrix which contains the rotation moment around specified axis.
		/// </summary>
		/// <param name="axis">The axis of rotation.
		/// <param name="angle">The angle of rotation in radians.
		/// <param name="result">The rotation matrix as an output parameter.
		public static void CreateFromAxisAngle(
			Vector3 axis,
			float angle,
			out Matrix4 result
		) {
			float x = axis.mX;
			float y = axis.mY;
			float z = axis.mZ;
			float num2 = (float) Math.Sin((double) angle);
			float num = (float) Math.Cos((double) angle);
			float num11 = x * x;
			float num10 = y * y;
			float num9 = z * z;
			float num8 = x * y;
			float num7 = x * z;
			float num6 = y * z;
			result.m00 = num11 + (num * (1f - num11));
			result.m10 = (num8 - (num * num8)) + (num2 * z);
			result.m20 = (num7 - (num * num7)) - (num2 * y);
			result.m30 = 0;
			result.m01 = (num8 - (num * num8)) - (num2 * z);
			result.m11 = num10 + (num * (1f - num10));
			result.m21 = (num6 - (num * num6)) + (num2 * x);
			result.m31 = 0;
			result.m02 = (num7 - (num * num7)) + (num2 * y);
			result.m12 = (num6 - (num * num6)) - (num2 * x);
			result.m22 = num9 + (num * (1f - num9));
			result.m32 = 0;
			result.m03 = 0;
			result.m13 = 0;
			result.m23 = 0;
			result.m33 = 1;
		}

		/// <summary>
		/// Creates a new rotation matrix from a <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="quaternion"><see cref="Quaternion"/> of rotation moment.
		/// <returns>The rotation matrix.</returns>
		public static Matrix4 CreateFromQuaternion(Quaternion quaternion)
		{
			Matrix4 result;
			CreateFromQuaternion(quaternion, out result);
			return result;
		}

		/// <summary>
		/// Creates a new rotation matrix from a <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="quaternion"><see cref="Quaternion"/> of rotation moment.
		/// <param name="result">The rotation matrix as an output parameter.
		public static void CreateFromQuaternion(Quaternion quaternion, out Matrix4 result)
		{
			float num9 = quaternion.mX * quaternion.mX;
			float num8 = quaternion.mY * quaternion.mY;
			float num7 = quaternion.mZ * quaternion.mZ;
			float num6 = quaternion.mX * quaternion.mY;
			float num5 = quaternion.mZ * quaternion.mW;
			float num4 = quaternion.mZ * quaternion.mX;
			float num3 = quaternion.mY * quaternion.mW;
			float num2 = quaternion.mY * quaternion.mZ;
			float num = quaternion.mX * quaternion.mW;
			result.m00 = 1f - (2f * (num8 + num7));
			result.m10 = 2f * (num6 + num5);
			result.m20 = 2f * (num4 - num3);
			result.m30 = 0f;
			result.m01 = 2f * (num6 - num5);
			result.m11 = 1f - (2f * (num7 + num9));
			result.m21 = 2f * (num2 + num);
			result.m31 = 0f;
			result.m02 = 2f * (num4 + num3);
			result.m12 = 2f * (num2 - num);
			result.m22 = 1f - (2f * (num8 + num9));
			result.m32 = 0f;
			result.m03 = 0f;
			result.m13 = 0f;
			result.m23 = 0f;
			result.m33 = 1f;
		}

		/// Creates a new rotation matrix from the specified yaw, pitch and roll values.
		/// @param yaw The yaw rotation value in radians.
		/// @param pitch The pitch rotation value in radians.
		/// @param roll The roll rotation value in radians.
		/// @returns The rotation matrix
		/// @remarks For more information about yaw, pitch and roll visit http://en.wikipedia.org/wiki/Euler_angles.
		public static Matrix4 CreateFromYawPitchRoll(float yaw, float pitch, float roll)
		{
			Matrix4 matrix;
			CreateFromYawPitchRoll(yaw, pitch, roll, out matrix);
			return matrix;
		}

		/// Creates a new rotation matrix from the specified yaw, pitch and roll values.
		/// @param yaw The yaw rotation value in radians.
		/// @param pitch The pitch rotation value in radians.
		/// @param roll The roll rotation value in radians.
		/// @param result The rotation matrix as an output parameter.
		/// @remarks>For more information about yaw, pitch and roll visit http://en.wikipedia.org/wiki/Euler_angles.
		public static void CreateFromYawPitchRoll(
			float yaw,
			float pitch,
			float roll,
			out Matrix4 result
		) {
			Quaternion quaternion;
			Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll, out quaternion);
			CreateFromQuaternion(quaternion, out result);
		}

		/// Creates a new viewing matrix.
		/// @param cameraPosition Position of the camera.
		/// @param cameraTarget Lookup vector of the camera.
		/// @param cameraUpVector The direction of the upper edge of the camera.
		/// @returns The viewing matrix.
		public static Matrix4 CreateLookAt(
			Vector3 cameraPosition,
			Vector3 cameraTarget,
			Vector3 cameraUpVector
		) {
			Matrix4 matrix;
			CreateLookAt(cameraPosition, cameraTarget, cameraUpVector, out matrix);
			return matrix;
		}

		/// Creates a new viewing matrix.
		/// @param cameraPosition Position of the camera.
		/// @param cameraTarget Lookup vector of the camera.
		/// @param cameraUpVector The direction of the upper edge of the camera.
		/// @param result The viewing matrix as an output parameter.
		public static void CreateLookAt(
			Vector3 cameraPosition,
			Vector3 cameraTarget,
			Vector3 cameraUpVector,
			out Matrix4 result
		) {
			Vector3 vectorA = Vector3.Normalize(cameraPosition - cameraTarget);
			Vector3 vectorB = Vector3.Normalize(Vector3.Cross(cameraUpVector, vectorA));
			Vector3 vectorC = Vector3.Cross(vectorA, vectorB);
			result.m00 = vectorB.mX;
			result.m10 = vectorC.mX;
			result.m20 = vectorA.mX;
			result.m30 = 0f;
			result.m01 = vectorB.mY;
			result.m11 = vectorC.mY;
			result.m21 = vectorA.mY;
			result.m31 = 0f;
			result.m02 = vectorB.mZ;
			result.m12 = vectorC.mZ;
			result.m22 = vectorA.mZ;
			result.m32 = 0f;
			result.m03 = -Vector3.Dot(vectorB, cameraPosition);
			result.m13 = -Vector3.Dot(vectorC, cameraPosition);
			result.m23 = -Vector3.Dot(vectorA, cameraPosition);
			result.m33 = 1f;
		}

		/// Creates a new projection matrix for orthographic view.
		/// @param width Width of the viewing volume.
		/// @param height Height of the viewing volume.
		/// @param zNearPlane Depth of the near plane.
		/// @param zFarPlane Depth of the far plane.
		/// @returns The new projection matrix for orthographic view.</returns>
		public static Matrix4 CreateOrthographic(
			float width,
			float height,
			float zNearPlane,
			float zFarPlane
		) {
			Matrix4 matrix;
			CreateOrthographic(width, height, zNearPlane, zFarPlane, out matrix);
			return matrix;
		}

		/// Creates a new projection matrix for orthographic view.
		/// @param width Width of the viewing volume.
		/// @param height Height of the viewing volume.
		/// @param zNearPlane Depth of the near plane.
		/// @param zFarPlane Depth of the far plane.
		/// @param result The new projection matrix for orthographic view as an output parameter.
		public static void CreateOrthographic(
			float width,
			float height,
			float zNearPlane,
			float zFarPlane,
			out Matrix4 result
		) {
			result.m00 = 2f / width;
			result.m10 = result.m20 = result.m30 = 0f;
			result.m11 = 2f / height;
			result.m01 = result.m21 = result.m31 = 0f;
			result.m22 = 1f / (zNearPlane - zFarPlane);
			result.m02 = result.m12 = result.m32 = 0f;
			result.m03 = result.m13 = 0f;
			result.m23 = zNearPlane / (zNearPlane - zFarPlane);
			result.m33 = 1f;
		}

		/// Creates a new projection matrix for customized orthographic view.
		/// @param left Lower x-value at the near plane.
		/// @param right Upper x-value at the near plane.
		/// @param bottom Lower y-coordinate at the near plane.
		/// @param top Upper y-value at the near plane.
		/// @param zNearPlane Depth of the near plane.
		/// @param zFarPlane Depth of the far plane.
		/// @returns The new projection matrix for customized orthographic view.</returns>
		public static Matrix4 CreateOrthographicOffCenter(
			float left,
			float right,
			float bottom,
			float top,
			float zNearPlane,
			float zFarPlane
		) {
			Matrix4 matrix;
			CreateOrthographicOffCenter(
				left,
				right,
				bottom,
				top,
				zNearPlane,
				zFarPlane,
				out matrix
			);
			return matrix;
		}

		/// Creates a new projection matrix for customized orthographic view.
		/// @param left Lower x-value at the near plane.
		/// @param right Upper x-value at the near plane.
		/// @param bottom Lower y-coordinate at the near plane.
		/// @param top Upper y-value at the near plane.
		/// @param zNearPlane Depth of the near plane.
		/// @param zFarPlane Depth of the far plane.
		/// @param result The new projection matrix for customized orthographic view as an output parameter.
		public static void CreateOrthographicOffCenter(
			float left,
			float right,
			float bottom,
			float top,
			float zNearPlane,
			float zFarPlane,
			out Matrix4 result
		)
		{
			result.m00 = (float) (2.0 / ((double) right - (double) left));
			result.m10 = 0.0f;
			result.m20 = 0.0f;
			result.m30 = 0.0f;
			result.m01 = 0.0f;
			result.m11 = (float) (2.0 / ((double) top - (double) bottom));
			result.m21 = 0.0f;
			result.m31 = 0.0f;
			result.m02 = 0.0f;
			result.m12 = 0.0f;
			result.m22 = (float) (1.0 / ((double) zNearPlane - (double) zFarPlane));
			result.m32 = 0.0f;
			result.m03 = (float) (
				((double) left + (double) right) /
				((double) left - (double) right)
			);
			result.m13 = (float) (
				((double) top + (double) bottom) /
				((double) bottom - (double) top)
			);
			result.m23 = (float) (
				(double) zNearPlane /
				((double) zNearPlane - (double) zFarPlane)
			);
			result.m33 = 1.0f;
		}

		/// Creates a new matrix that flattens geometry into a specified <see cref="Plane"/> as if casting a shadow from a specified light source.
		/// @param lightDirection A vector specifying the direction from which the light that will cast the shadow is coming.
		/// @param plane The plane onto which the new matrix should flatten geometry so as to cast a shadow.
		/// @returns>A matrix that can be used to flatten geometry onto the specified plane from the specified direction.
		public static Matrix4 CreateShadow(Vector3 lightDirection, Plane plane)
		{
			Matrix4 result;
			result = CreateShadow(lightDirection, plane);
			return result;
		}

		/// Creates a new matrix that flattens geometry into a specified <see cref="Plane"/> as if casting a shadow from a specified light source.
		/// @param lightDirection A vector specifying the direction from which the light that will cast the shadow is coming.
		/// @param plane The plane onto which the new matrix should flatten geometry so as to cast a shadow.
		/// @param result A matrix that can be used to flatten geometry onto the specified plane from the specified direction as an output parameter.
		public static void CreateShadow(
			Vector3 lightDirection,
			Plane plane,
			out Matrix4 result)
		{
			float dot = (
				(plane.Normal.mX * lightDirection.mX) +
				(plane.Normal.mY * lightDirection.mY) +
				(plane.Normal.mZ * lightDirection.mZ)
			);
			float x = -plane.Normal.mX;
			float y = -plane.Normal.mY;
			float z = -plane.Normal.mZ;
			float d = -plane.D;

			result.m00 = (x * lightDirection.mX) + dot;
			result.m10 = x * lightDirection.mY;
			result.m20 = x * lightDirection.mZ;
			result.m30 = 0;
			result.m01 = y * lightDirection.mX;
			result.m11 = (y * lightDirection.mY) + dot;
			result.m21 = y * lightDirection.mZ;
			result.m31 = 0;
			result.m02 = z * lightDirection.mX;
			result.m12 = z * lightDirection.mY;
			result.m22 = (z * lightDirection.mZ) + dot;
			result.m32 = 0;
			result.m03 = d * lightDirection.mX;
			result.m13 = d * lightDirection.mY;
			result.m23 = d * lightDirection.mZ;
			result.m33 = dot;
		}

		public override void ToString(System.String strBuffer)
		{
			for (int row < 4)
				for (int col < 4)
				{
#unwarn
					strBuffer.AppendF($"M{row+1}{col+1}:{((float*)&this)[row+col*4]}\n");
				}
		}
    }
}
