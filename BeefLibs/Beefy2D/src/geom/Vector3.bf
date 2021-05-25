// This file contains portions of code from the FNA project (github.com/FNA-XNA/FNA),
// released under the Microsoft Public License

using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;

namespace Beefy.geom
{
    public struct Vector3 : IHashable, IEquatable<Vector3>
    {
		[Reflect]
        public float mX;
		[Reflect]
        public float mY;
		[Reflect]
        public float mZ;

        private static Vector3 sZero = Vector3(0f, 0f, 0f);
        private static Vector3 sOne = Vector3(1f, 1f, 1f);
        private static Vector3 sUnitX = Vector3(1f, 0f, 0f);
        private static Vector3 sUnitY = Vector3(0f, 1f, 0f);
        private static Vector3 sUnitZ = Vector3(0f, 0f, 1f);
        private static Vector3 sUp = Vector3(0f, 1f, 0f);
        private static Vector3 sDown = Vector3(0f, -1f, 0f);
        private static Vector3 sRight = Vector3(1f, 0f, 0f);
        private static Vector3 sLeft = Vector3(-1f, 0f, 0f);
        private static Vector3 sForward = Vector3(0f, 0f, -1f);
        private static Vector3 sBackward = Vector3(0f, 0f, 1f);

        public static Vector3 Zero
        {
            get { return sZero; }
        }

        public static Vector3 One
        {
            get { return sOne; }
        }

        public static Vector3 UnitX
        {
            get { return sUnitX; }
        }

        public static Vector3 UnitY
        {
            get { return sUnitY; }
        }

        public static Vector3 UnitZ
        {
            get { return sUnitZ; }
        }

        public static Vector3 Up
        {
            get { return sUp; }
        }

        public static Vector3 Down
        {
            get { return sDown; }
        }

        public static Vector3 Right
        {
            get { return sRight; }
        }

        public static Vector3 Left
        {
            get { return sLeft; }
        }

        public static Vector3 Forward
        {
            get { return sForward; }
        }

        public static Vector3 Backward
        {
            get { return sBackward; }
        }

        public float Length
        {
            get
            {
                return (float)Math.Sqrt(mX * mX + mY * mY + mZ * mZ);
            }
        }

        public float LengthSquared
        {
            get
            {
                return mX * mX + mY * mY + mZ * mZ;
            }
        }

        public this(float x, float y, float z)
        {
            mX = x;
            mY = y;
            mZ = z;
        }

        public bool Equals(Vector3 other)
        {
            return this == other;
        }

        public int GetHashCode()
        {
            return (int)(this.mX + this.mY + this.mZ);
        }


        /*public static Vector2D Add(Vector2D vec1, Vector2D vec2)
        {
            return new Vector2D(vec1.mX + vec2.mX, vec1.mY + vec2.mY);
        }

        public static Vector2D Subtract(Vector2D vec1, Vector2D vec2)
        {
            return new Vector2D(vec1.mX - vec2.mX, vec1.mY - vec2.mY);
        }*/
        

        public static Vector3 Normalize(Vector3 vector)
        {
			Vector3 newVec;
            Normalize(vector, out newVec);
            return newVec;
        }

        public static void Normalize(Vector3 value, out Vector3 result)
        {
            float factor= Distance(value, sZero);
            factor = 1f / factor;
            result.mX = value.mX * factor;
            result.mY = value.mY * factor;
            result.mZ = value.mZ * factor;
        }

        public static float Dot(Vector3 vec1, Vector3 vec2)
        {
            return vec1.mX * vec2.mX + vec1.mY * vec2.mY + vec1.mZ * vec2.mZ;
        }

        public static Vector3 Cross(Vector3 vector1, Vector3 vector2)
        {
            return Vector3(vector1.mY * vector2.mZ - vector2.mY * vector1.mZ,
                                 -(vector1.mX * vector2.mZ - vector2.mX * vector1.mZ),
                                 vector1.mX * vector2.mY - vector2.mX * vector1.mY);
        }

        public static float DistanceSquared(Vector3 value1, Vector3 value2)
        {
            return (value1.mX - value2.mX) * (value1.mX - value2.mX) +
                     (value1.mY - value2.mY) * (value1.mY - value2.mY) +
                     (value1.mZ - value2.mZ) * (value1.mZ - value2.mZ);
        }

        public static float Distance(Vector3 vector1, Vector3 vector2)
        {
            float result = DistanceSquared(vector1, vector2);
            return (float)Math.Sqrt(result);
        }

        /*public static Vector2D FromAngle(float angle, float length = 1.0f)
        {
            return new Vector2D((float)Math.Cos(angle) * length, (float)Math.Sin(angle) * length);
        }*/

        public static Vector3 TransformW(Vector3 vec, Matrix4 matrix)
        {
			Vector3 result;
            float fInvW = 1.0f / (matrix.m30 * vec.mX + matrix.m31 * vec.mY + matrix.m32 * vec.mZ + matrix.m33);

            result.mX = (matrix.m00 * vec.mX + matrix.m01 * vec.mY + matrix.m02 * vec.mZ + matrix.m03) * fInvW;
            result.mY = (matrix.m10 * vec.mX + matrix.m11 * vec.mY + matrix.m12 * vec.mZ + matrix.m13) * fInvW;
            result.mZ = (matrix.m20 * vec.mX + matrix.m21 * vec.mY + matrix.m22 * vec.mZ + matrix.m23) * fInvW;

			return result;
        }

		public static Vector3 Transform(Vector3 vec, Matrix4 matrix)
		{
			Vector3 result;
			result.mX = (vec.mX * matrix.m00) + (vec.mY * matrix.m01) + (vec.mZ * matrix.m02) + matrix.m03;
			result.mY = (vec.mX * matrix.m10) + (vec.mY * matrix.m11) + (vec.mZ * matrix.m12) + matrix.m13;
			result.mZ = (vec.mX * matrix.m20) + (vec.mY * matrix.m21) + (vec.mZ * matrix.m22) + matrix.m23;
			return result;
		}

        /*public static void Transform(Vector3[] sourceArray, ref Matrix4 matrix, Vector3[] destinationArray)
        {
            //Debug.Assert(destinationArray.Length >= sourceArray.Length, "The destination array is smaller than the source array.");
            
            for (var i = 0; i < sourceArray.Length; i++)
            {
                var position = sourceArray[i];
                destinationArray[i] =
                    new Vector3(
                        (position.mX * matrix.m11) + (position.mY * matrix.m21) + (position.mZ * matrix.m31) + matrix.m41,
                        (position.mX * matrix.m12) + (position.mY * matrix.m22) + (position.mZ * matrix.m32) + matrix.m42,
                        (position.mX * matrix.m13) + (position.mY * matrix.m23) + (position.mZ * matrix.m33) + matrix.m43);
            }
        }*/

		/// <summary>
		/// Returns a <see>Vector3</see> pointing in the opposite
		/// direction of <paramref name="value"/>.
		/// </summary>
		/// <param name="value">The vector to negate.</param>
		/// <returns>The vector negation of <paramref name="value"/>.</returns>
		public static Vector3 Negate(Vector3 value)
		{
		    return .(-value.mX, -value.mY, -value.mZ);
		}

		/// <summary>
		/// Stores a <see>Vector3</see> pointing in the opposite
		/// direction of <paramref name="value"/> in <paramref name="result"/>.
		/// </summary>
		/// <param name="value">The vector to negate.</param>
		/// <param name="result">The vector that the negation of <paramref name="value"/> will be stored in.</param>
		public static void Negate(Vector3 value, out Vector3 result)
		{
		    result.mX = -value.mX;
		    result.mY = -value.mY;
		    result.mZ = -value.mZ;
		}

		/// <summary>
		/// Creates a new <see cref="Vector3"/> that contains a multiplication of two vectors.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector3"/>.</param>
		/// <param name="value2">Source <see cref="Vector3"/>.</param>
		/// <returns>The result of the vector multiplication.</returns>
		public static Vector3 Multiply(Vector3 value1, Vector3 value2)
		{
			return .(value1.mX * value2.mX, value1.mY * value2.mY, value1.mZ * value2.mZ);
		}

		public static Vector3 Multiply(Vector3 value1, float value2)
		{
			return .(value1.mX * value2, value1.mY * value2, value1.mZ * value2);
		}

		public void Normalize() mut
		{
		    Normalize(this, out this);
		}

        public static Vector3 Transform(Vector3 vec, Quaternion quat)
        {        
            Matrix4 matrix = quat.ToMatrix();
            return Transform(vec, matrix);
        }

        public static Vector3 TransformNormal(Vector3 normal, Matrix4 matrix)
        {
            return Vector3((normal.mX * matrix.m11) + (normal.mY * matrix.m21) + (normal.mZ * matrix.m31),
                                 (normal.mX * matrix.m12) + (normal.mY * matrix.m22) + (normal.mZ * matrix.m32),
                                 (normal.mX * matrix.m13) + (normal.mY * matrix.m23) + (normal.mZ * matrix.m33));
        }

        public static bool operator ==(Vector3 value1, Vector3 value2)
        {
            return (value1.mX == value2.mX) &&
                (value1.mY == value2.mY) &&
                (value1.mZ == value2.mZ);
        }

        public static bool operator !=(Vector3 value1, Vector3 value2)
        {
            return !(value1 == value2);
        }

        public static Vector3 operator +(Vector3 vec1, Vector3 vec2)
        {
            return Vector3(vec1.mX + vec2.mX, vec1.mY + vec2.mY, vec1.mZ + vec2.mZ);
        }        

        public static Vector3 operator -(Vector3 vec1, Vector3 vec2)
        {
            return Vector3(vec1.mX - vec2.mX, vec1.mY - vec2.mY, vec1.mZ - vec2.mZ);
        }

		public static Vector3 operator -(Vector3 vec1)
		{
		    return Vector3(-vec1.mX, -vec1.mY, -vec1.mZ);
		}

        public static Vector3 operator *(Vector3 vec, float scale)
        {
            return Vector3(vec.mX * scale, vec.mY * scale, vec.mZ * scale);
        }

        public override void ToString(String str)
        {
            str.AppendF("{0:0.0#}, {1:0.0#}, {2:0.0#}", mX, mY, mZ);
        }
    }
}
