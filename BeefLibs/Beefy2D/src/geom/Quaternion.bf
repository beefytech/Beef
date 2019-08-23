using System;
using Beefy.gfx;

namespace Beefy.geom
{
    public struct Quaternion : IHashable, IEquatable<Quaternion>
    {
        public float mX;
        public float mY;
        public float mZ;
        public float mW;
        public static readonly Quaternion Identity = Quaternion(0, 0, 0, 1);
        
        public this(float x, float y, float z, float w)
        {
            mX = x;
            mY = y;
            mZ = z;
            mW = w;
        }
        
        public this(Vector3 vectorPart, float scalarPart)
        {
            mX = vectorPart.mX;
            mY = vectorPart.mY;
            mZ = vectorPart.mZ;
            mW = scalarPart;
        }

        public static Quaternion Add(Quaternion quaternion1, Quaternion quaternion2)
        {            
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX + quaternion2.mX;
            quaternion.mY = quaternion1.mY + quaternion2.mY;
            quaternion.mZ = quaternion1.mZ + quaternion2.mZ;
            quaternion.mW = quaternion1.mW + quaternion2.mW;
            return quaternion;
        }


        public static void Add(ref Quaternion quaternion1, ref Quaternion quaternion2, out Quaternion result)
        {            
            result.mX = quaternion1.mX + quaternion2.mX;
            result.mY = quaternion1.mY + quaternion2.mY;
            result.mZ = quaternion1.mZ + quaternion2.mZ;
            result.mW = quaternion1.mW + quaternion2.mW;
        }

        public static Quaternion Concatenate(Quaternion value1, Quaternion value2)
        {
            Quaternion quaternion;
            float x = value2.mX;
            float y = value2.mY;
            float z = value2.mZ;
            float w = value2.mW;
            float num4 = value1.mX;
            float num3 = value1.mY;
            float num2 = value1.mZ;
            float num = value1.mW;
            float num12 = (y * num2) - (z * num3);
            float num11 = (z * num4) - (x * num2);
            float num10 = (x * num3) - (y * num4);
            float num9 = ((x * num4) + (y * num3)) + (z * num2);
            quaternion.mX = ((x * num) + (num4 * w)) + num12;
            quaternion.mY = ((y * num) + (num3 * w)) + num11;
            quaternion.mZ = ((z * num) + (num2 * w)) + num10;
            quaternion.mW = (w * num) - num9;
            return quaternion;
        }

        public static void Concatenate(ref Quaternion value1, ref Quaternion value2, out Quaternion result)
        {
            float x = value2.mX;
            float y = value2.mY;
            float z = value2.mZ;
            float w = value2.mW;
            float num4 = value1.mX;
            float num3 = value1.mY;
            float num2 = value1.mZ;
            float num = value1.mW;
            float num12 = (y * num2) - (z * num3);
            float num11 = (z * num4) - (x * num2);
            float num10 = (x * num3) - (y * num4);
            float num9 = ((x * num4) + (y * num3)) + (z * num2);
            result.mX = ((x * num) + (num4 * w)) + num12;
            result.mY = ((y * num) + (num3 * w)) + num11;
            result.mZ = ((z * num) + (num2 * w)) + num10;
            result.mW = (w * num) - num9;
        }
                
        public void Conjugate() mut
        {
            mX = -mX;
            mY = -mY;
            mZ = -mZ;
        }
                
        public static Quaternion Conjugate(Quaternion value)
        {
            Quaternion quaternion;
            quaternion.mX = -value.mX;
            quaternion.mY = -value.mY;
            quaternion.mZ = -value.mZ;
            quaternion.mW = value.mW;
            return quaternion;
        }

        public static void Conjugate(ref Quaternion value, out Quaternion result)
        {
            result.mX = -value.mX;
            result.mY = -value.mY;
            result.mZ = -value.mZ;
            result.mW = value.mW;
        }

        public static Quaternion CreateFromAxisAngle(Vector3 axis, float angle)
        {
            Quaternion quaternion;
            float num2 = angle * 0.5f;
            float num = (float)Math.Sin((double)num2);
            float num3 = (float)Math.Cos((double)num2);
            quaternion.mX = axis.mX * num;
            quaternion.mY = axis.mY * num;
            quaternion.mZ = axis.mZ * num;
            quaternion.mW = num3;
            return quaternion;
        }

        public static void CreateFromAxisAngle(ref Vector3 axis, float angle, out Quaternion result)
        {
            float num2 = angle * 0.5f;
            float num = (float)Math.Sin((double)num2);
            float num3 = (float)Math.Cos((double)num2);
            result.mX = axis.mX * num;
            result.mY = axis.mY * num;
            result.mZ = axis.mZ * num;
            result.mW = num3;
        }

        public static Quaternion CreateFromRotationMatrix(Matrix4 matrix)
        {
            float num8 = (matrix.m11 + matrix.m22) + matrix.m33;
            Quaternion quaternion = Quaternion();
            if (num8 > 0f)
            {
                float num = (float)Math.Sqrt((double)(num8 + 1f));
                quaternion.mW = num * 0.5f;
                num = 0.5f / num;
                quaternion.mX = (matrix.m23 - matrix.m32) * num;
                quaternion.mY = (matrix.m31 - matrix.m13) * num;
                quaternion.mZ = (matrix.m12 - matrix.m21) * num;
                return quaternion;
            }
            if ((matrix.m11 >= matrix.m22) && (matrix.m11 >= matrix.m33))
            {
                float num7 = (float)Math.Sqrt((double)(((1f + matrix.m11) - matrix.m22) - matrix.m33));
                float num4 = 0.5f / num7;
                quaternion.mX = 0.5f * num7;
                quaternion.mY = (matrix.m12 + matrix.m21) * num4;
                quaternion.mZ = (matrix.m13 + matrix.m31) * num4;
                quaternion.mW = (matrix.m23 - matrix.m32) * num4;
                return quaternion;
            }
            if (matrix.m22 > matrix.m33)
            {
                float num6 = (float)Math.Sqrt((double)(((1f + matrix.m22) - matrix.m11) - matrix.m33));
                float num3 = 0.5f / num6;
                quaternion.mX = (matrix.m21 + matrix.m12) * num3;
                quaternion.mY = 0.5f * num6;
                quaternion.mZ = (matrix.m32 + matrix.m23) * num3;
                quaternion.mW = (matrix.m31 - matrix.m13) * num3;
                return quaternion;
            }
            float num5 = (float)Math.Sqrt((double)(((1f + matrix.m33) - matrix.m11) - matrix.m22));
            float num2 = 0.5f / num5;
            quaternion.mX = (matrix.m31 + matrix.m13) * num2;
            quaternion.mY = (matrix.m32 + matrix.m23) * num2;
            quaternion.mZ = 0.5f * num5;
            quaternion.mW = (matrix.m12 - matrix.m21) * num2;

            return quaternion;

        }

        public static void CreateFromRotationMatrix(ref Matrix4 matrix, out Quaternion result)
        {
            float num8 = (matrix.m11 + matrix.m22) + matrix.m33;
            if (num8 > 0f)
            {
                float num = (float)Math.Sqrt((double)(num8 + 1f));
                result.mW = num * 0.5f;
                num = 0.5f / num;
                result.mX = (matrix.m23 - matrix.m32) * num;
                result.mY = (matrix.m31 - matrix.m13) * num;
                result.mZ = (matrix.m12 - matrix.m21) * num;
            }
            else if ((matrix.m11 >= matrix.m22) && (matrix.m11 >= matrix.m33))
            {
                float num7 = (float)Math.Sqrt((double)(((1f + matrix.m11) - matrix.m22) - matrix.m33));
                float num4 = 0.5f / num7;
                result.mX = 0.5f * num7;
                result.mY = (matrix.m12 + matrix.m21) * num4;
                result.mZ = (matrix.m13 + matrix.m31) * num4;
                result.mW = (matrix.m23 - matrix.m32) * num4;
            }
            else if (matrix.m22 > matrix.m33)
            {
                float num6 = (float)Math.Sqrt((double)(((1f + matrix.m22) - matrix.m11) - matrix.m33));
                float num3 = 0.5f / num6;
                result.mX = (matrix.m21 + matrix.m12) * num3;
                result.mY = 0.5f * num6;
                result.mZ = (matrix.m32 + matrix.m23) * num3;
                result.mW = (matrix.m31 - matrix.m13) * num3;
            }
            else
            {
                float num5 = (float)Math.Sqrt((double)(((1f + matrix.m33) - matrix.m11) - matrix.m22));
                float num2 = 0.5f / num5;
                result.mX = (matrix.m31 + matrix.m13) * num2;
                result.mY = (matrix.m32 + matrix.m23) * num2;
                result.mZ = 0.5f * num5;
                result.mW = (matrix.m12 - matrix.m21) * num2;
            }
        }

        public static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll)
        {
            Quaternion quaternion;
            float num9 = roll * 0.5f;
            float num6 = (float)Math.Sin((double)num9);
            float num5 = (float)Math.Cos((double)num9);
            float num8 = pitch * 0.5f;
            float num4 = (float)Math.Sin((double)num8);
            float num3 = (float)Math.Cos((double)num8);
            float num7 = yaw * 0.5f;
            float num2 = (float)Math.Sin((double)num7);
            float num = (float)Math.Cos((double)num7);
            quaternion.mX = ((num * num4) * num5) + ((num2 * num3) * num6);
            quaternion.mY = ((num2 * num3) * num5) - ((num * num4) * num6);
            quaternion.mZ = ((num * num3) * num6) - ((num2 * num4) * num5);
            quaternion.mW = ((num * num3) * num5) + ((num2 * num4) * num6);
            return quaternion;
        }

        public static void CreateFromYawPitchRoll(float yaw, float pitch, float roll, out Quaternion result)
        {
            float num9 = roll * 0.5f;
            float num6 = (float)Math.Sin((double)num9);
            float num5 = (float)Math.Cos((double)num9);
            float num8 = pitch * 0.5f;
            float num4 = (float)Math.Sin((double)num8);
            float num3 = (float)Math.Cos((double)num8);
            float num7 = yaw * 0.5f;
            float num2 = (float)Math.Sin((double)num7);
            float num = (float)Math.Cos((double)num7);
            result.mX = ((num * num4) * num5) + ((num2 * num3) * num6);
            result.mY = ((num2 * num3) * num5) - ((num * num4) * num6);
            result.mZ = ((num * num3) * num6) - ((num2 * num4) * num5);
            result.mW = ((num * num3) * num5) + ((num2 * num4) * num6);
        }

        public static Quaternion Divide(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num14 = (((quaternion2.mX * quaternion2.mX) + (quaternion2.mY * quaternion2.mY)) + (quaternion2.mZ * quaternion2.mZ)) + (quaternion2.mW * quaternion2.mW);
            float num5 = 1f / num14;
            float num4 = -quaternion2.mX * num5;
            float num3 = -quaternion2.mY * num5;
            float num2 = -quaternion2.mZ * num5;
            float num = quaternion2.mW * num5;
            float num13 = (y * num2) - (z * num3);
            float num12 = (z * num4) - (x * num2);
            float num11 = (x * num3) - (y * num4);
            float num10 = ((x * num4) + (y * num3)) + (z * num2);
            quaternion.mX = ((x * num) + (num4 * w)) + num13;
            quaternion.mY = ((y * num) + (num3 * w)) + num12;
            quaternion.mZ = ((z * num) + (num2 * w)) + num11;
            quaternion.mW = (w * num) - num10;
            return quaternion;
        }

        public static void Divide(ref Quaternion quaternion1, ref Quaternion quaternion2, out Quaternion result)
        {
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num14 = (((quaternion2.mX * quaternion2.mX) + (quaternion2.mY * quaternion2.mY)) + (quaternion2.mZ * quaternion2.mZ)) + (quaternion2.mW * quaternion2.mW);
            float num5 = 1f / num14;
            float num4 = -quaternion2.mX * num5;
            float num3 = -quaternion2.mY * num5;
            float num2 = -quaternion2.mZ * num5;
            float num = quaternion2.mW * num5;
            float num13 = (y * num2) - (z * num3);
            float num12 = (z * num4) - (x * num2);
            float num11 = (x * num3) - (y * num4);
            float num10 = ((x * num4) + (y * num3)) + (z * num2);
            result.mX = ((x * num) + (num4 * w)) + num13;
            result.mY = ((y * num) + (num3 * w)) + num12;
            result.mZ = ((z * num) + (num2 * w)) + num11;
            result.mW = (w * num) - num10;
        }

        public static float Dot(Quaternion quaternion1, Quaternion quaternion2)
        {
            return ((((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW));
        }

        public static void Dot(ref Quaternion quaternion1, ref Quaternion quaternion2, out float result)
        {
            result = (((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW);
        }

        public bool Equals(Quaternion other)
        {
            return (mX == other.mX) && (mY == other.mY) && (mZ == other.mZ) && (mW == other.mW);
        }

        public int GetHashCode()
        {
            //return ((mX.GetHashCode() + mY.GetHashCode()) + mZ.GetHashCode()) + mW.GetHashCode();
			ThrowUnimplemented();
        }

        public static Quaternion Inverse(Quaternion quaternion)
        {
            Quaternion quaternion2;
            float num2 = (((quaternion.mX * quaternion.mX) + (quaternion.mY * quaternion.mY)) + (quaternion.mZ * quaternion.mZ)) + (quaternion.mW * quaternion.mW);
            float num = 1f / num2;
            quaternion2.mX = -quaternion.mX * num;
            quaternion2.mY = -quaternion.mY * num;
            quaternion2.mZ = -quaternion.mZ * num;
            quaternion2.mW = quaternion.mW * num;
            return quaternion2;
        }

        public static void Inverse(ref Quaternion quaternion, out Quaternion result)
        {
            float num2 = (((quaternion.mX * quaternion.mX) + (quaternion.mY * quaternion.mY)) + (quaternion.mZ * quaternion.mZ)) + (quaternion.mW * quaternion.mW);
            float num = 1f / num2;
            result.mX = -quaternion.mX * num;
            result.mY = -quaternion.mY * num;
            result.mZ = -quaternion.mZ * num;
            result.mW = quaternion.mW * num;
        }

        public float Length()
        {
            float num = (((mX * mX) + (mY * mY)) + (mZ * mZ)) + (mW * mW);
            return (float)Math.Sqrt((double)num);
        }

        public float LengthSquared()
        {
            return ((((mX * mX) + (mY * mY)) + (mZ * mZ)) + (mW * mW));
        }

        public static Quaternion Lerp(Quaternion quaternion1, Quaternion quaternion2, float amount)
        {
            float num = amount;
            float num2 = 1f - num;
            Quaternion quaternion = Quaternion();
            float num5 = (((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW);
            if (num5 >= 0f)
            {
                quaternion.mX = (num2 * quaternion1.mX) + (num * quaternion2.mX);
                quaternion.mY = (num2 * quaternion1.mY) + (num * quaternion2.mY);
                quaternion.mZ = (num2 * quaternion1.mZ) + (num * quaternion2.mZ);
                quaternion.mW = (num2 * quaternion1.mW) + (num * quaternion2.mW);
            }
            else
            {
                quaternion.mX = (num2 * quaternion1.mX) - (num * quaternion2.mX);
                quaternion.mY = (num2 * quaternion1.mY) - (num * quaternion2.mY);
                quaternion.mZ = (num2 * quaternion1.mZ) - (num * quaternion2.mZ);
                quaternion.mW = (num2 * quaternion1.mW) - (num * quaternion2.mW);
            }
            float num4 = (((quaternion.mX * quaternion.mX) + (quaternion.mY * quaternion.mY)) + (quaternion.mZ * quaternion.mZ)) + (quaternion.mW * quaternion.mW);
            float num3 = 1f / ((float)Math.Sqrt((double)num4));
            quaternion.mX *= num3;
            quaternion.mY *= num3;
            quaternion.mZ *= num3;
            quaternion.mW *= num3;
            return quaternion;
        }

        public static void Lerp(ref Quaternion quaternion1, ref Quaternion quaternion2, float amount, out Quaternion result)
        {
            float num = amount;
            float num2 = 1f - num;
            float num5 = (((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW);
            if (num5 >= 0f)
            {
                result.mX = (num2 * quaternion1.mX) + (num * quaternion2.mX);
                result.mY = (num2 * quaternion1.mY) + (num * quaternion2.mY);
                result.mZ = (num2 * quaternion1.mZ) + (num * quaternion2.mZ);
                result.mW = (num2 * quaternion1.mW) + (num * quaternion2.mW);
            }
            else
            {
                result.mX = (num2 * quaternion1.mX) - (num * quaternion2.mX);
                result.mY = (num2 * quaternion1.mY) - (num * quaternion2.mY);
                result.mZ = (num2 * quaternion1.mZ) - (num * quaternion2.mZ);
                result.mW = (num2 * quaternion1.mW) - (num * quaternion2.mW);
            }
            float num4 = (((result.mX * result.mX) + (result.mY * result.mY)) + (result.mZ * result.mZ)) + (result.mW * result.mW);
            float num3 = 1f / ((float)Math.Sqrt((double)num4));
            result.mX *= num3;
            result.mY *= num3;
            result.mZ *= num3;
            result.mW *= num3;
        }

        public static Quaternion Slerp(Quaternion quaternion1, Quaternion quaternion2, float amount)
        {
            float num2;
            float num3;
            Quaternion quaternion;
            float num = amount;
            float num4 = (((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW);
            bool flag = false;
            if (num4 < 0f)
            {
                flag = true;
                num4 = -num4;
            }
            if (num4 > 0.999999f)
            {
                num3 = 1f - num;
                num2 = flag ? -num : num;
            }
            else
            {
                float num5 = (float)Math.Acos((double)num4);
                float num6 = (float)(1.0 / Math.Sin((double)num5));
                num3 = ((float)Math.Sin((double)((1f - num) * num5))) * num6;
                num2 = flag ? (((float)(-Math.Sin((double)(num * num5))) * num6)) : (((float)Math.Sin((double)(num * num5))) * num6);
            }
            quaternion.mX = (num3 * quaternion1.mX) + (num2 * quaternion2.mX);
            quaternion.mY = (num3 * quaternion1.mY) + (num2 * quaternion2.mY);
            quaternion.mZ = (num3 * quaternion1.mZ) + (num2 * quaternion2.mZ);
            quaternion.mW = (num3 * quaternion1.mW) + (num2 * quaternion2.mW);
            return quaternion;
        }
        
        public static void Slerp(ref Quaternion quaternion1, ref Quaternion quaternion2, float amount, out Quaternion result)
        {
            float num2;
            float num3;
            float num = amount;
            float num4 = (((quaternion1.mX * quaternion2.mX) + (quaternion1.mY * quaternion2.mY)) + (quaternion1.mZ * quaternion2.mZ)) + (quaternion1.mW * quaternion2.mW);
            bool flag = false;
            if (num4 < 0f)
            {
                flag = true;
                num4 = -num4;
            }
            if (num4 > 0.999999f)
            {
                num3 = 1f - num;
                num2 = flag ? -num : num;
            }
            else
            {
                float num5 = (float)Math.Acos((double)num4);
                float num6 = (float)(1.0 / Math.Sin((double)num5));
                num3 = ((float)Math.Sin((double)((1f - num) * num5))) * num6;
                num2 = flag ? (((float)(-Math.Sin((double)(num * num5))) * num6)) : (((float)Math.Sin((double)(num * num5))) * num6);
            }
            result.mX = (num3 * quaternion1.mX) + (num2 * quaternion2.mX);
            result.mY = (num3 * quaternion1.mY) + (num2 * quaternion2.mY);
            result.mZ = (num3 * quaternion1.mZ) + (num2 * quaternion2.mZ);
            result.mW = (num3 * quaternion1.mW) + (num2 * quaternion2.mW);
        }


        public static Quaternion Subtract(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX - quaternion2.mX;
            quaternion.mY = quaternion1.mY - quaternion2.mY;
            quaternion.mZ = quaternion1.mZ - quaternion2.mZ;
            quaternion.mW = quaternion1.mW - quaternion2.mW;
            return quaternion;
        }
        
        public static void Subtract(ref Quaternion quaternion1, ref Quaternion quaternion2, out Quaternion result)
        {
            result.mX = quaternion1.mX - quaternion2.mX;
            result.mY = quaternion1.mY - quaternion2.mY;
            result.mZ = quaternion1.mZ - quaternion2.mZ;
            result.mW = quaternion1.mW - quaternion2.mW;
        }
        
        public static Quaternion Multiply(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num4 = quaternion2.mX;
            float num3 = quaternion2.mY;
            float num2 = quaternion2.mZ;
            float num = quaternion2.mW;
            float num12 = (y * num2) - (z * num3);
            float num11 = (z * num4) - (x * num2);
            float num10 = (x * num3) - (y * num4);
            float num9 = ((x * num4) + (y * num3)) + (z * num2);
            quaternion.mX = ((x * num) + (num4 * w)) + num12;
            quaternion.mY = ((y * num) + (num3 * w)) + num11;
            quaternion.mZ = ((z * num) + (num2 * w)) + num10;
            quaternion.mW = (w * num) - num9;
            return quaternion;
        }
        
        public static Quaternion Multiply(Quaternion quaternion1, float scaleFactor)
        {
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX * scaleFactor;
            quaternion.mY = quaternion1.mY * scaleFactor;
            quaternion.mZ = quaternion1.mZ * scaleFactor;
            quaternion.mW = quaternion1.mW * scaleFactor;
            return quaternion;
        }
        
        public static void Multiply(ref Quaternion quaternion1, float scaleFactor, out Quaternion result)
        {
            result.mX = quaternion1.mX * scaleFactor;
            result.mY = quaternion1.mY * scaleFactor;
            result.mZ = quaternion1.mZ * scaleFactor;
            result.mW = quaternion1.mW * scaleFactor;
        }
        
        public static void Multiply(ref Quaternion quaternion1, ref Quaternion quaternion2, out Quaternion result)
        {
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num4 = quaternion2.mX;
            float num3 = quaternion2.mY;
            float num2 = quaternion2.mZ;
            float num = quaternion2.mW;
            float num12 = (y * num2) - (z * num3);
            float num11 = (z * num4) - (x * num2);
            float num10 = (x * num3) - (y * num4);
            float num9 = ((x * num4) + (y * num3)) + (z * num2);
            result.mX = ((x * num) + (num4 * w)) + num12;
            result.mY = ((y * num) + (num3 * w)) + num11;
            result.mZ = ((z * num) + (num2 * w)) + num10;
            result.mW = (w * num) - num9;
        }
        
        public static Quaternion Negate(Quaternion quaternion)
        {
            Quaternion quaternion2;
            quaternion2.mX = -quaternion.mX;
            quaternion2.mY = -quaternion.mY;
            quaternion2.mZ = -quaternion.mZ;
            quaternion2.mW = -quaternion.mW;
            return quaternion2;
        }
        
        public static void Negate(ref Quaternion quaternion, out Quaternion result)
        {
            result.mX = -quaternion.mX;
            result.mY = -quaternion.mY;
            result.mZ = -quaternion.mZ;
            result.mW = -quaternion.mW;
        }
        
        public void Normalize() mut
        {
            float num2 = (((mX * mX) + (mY * mY)) + (mZ * mZ)) + (mW * mW);
            float num = 1f / ((float)Math.Sqrt((double)num2));
            mX *= num;
            mY *= num;
            mZ *= num;
            mW *= num;
        }
        
        public static Quaternion Normalize(Quaternion quaternion)
        {
            Quaternion quaternion2;
            float num2 = (((quaternion.mX * quaternion.mX) + (quaternion.mY * quaternion.mY)) + (quaternion.mZ * quaternion.mZ)) + (quaternion.mW * quaternion.mW);
            float num = 1f / ((float)Math.Sqrt((double)num2));
            quaternion2.mX = quaternion.mX * num;
            quaternion2.mY = quaternion.mY * num;
            quaternion2.mZ = quaternion.mZ * num;
            quaternion2.mW = quaternion.mW * num;
            return quaternion2;
        }
        
        public static void Normalize(ref Quaternion quaternion, out Quaternion result)
        {
            float num2 = (((quaternion.mX * quaternion.mX) + (quaternion.mY * quaternion.mY)) + (quaternion.mZ * quaternion.mZ)) + (quaternion.mW * quaternion.mW);
            float num = 1f / ((float)Math.Sqrt((double)num2));
            result.mX = quaternion.mX * num;
            result.mY = quaternion.mY * num;
            result.mZ = quaternion.mZ * num;
            result.mW = quaternion.mW * num;
        }
        
        public static Quaternion operator +(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX + quaternion2.mX;
            quaternion.mY = quaternion1.mY + quaternion2.mY;
            quaternion.mZ = quaternion1.mZ + quaternion2.mZ;
            quaternion.mW = quaternion1.mW + quaternion2.mW;
            return quaternion;
        }
        
        public static Quaternion operator /(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num14 = (((quaternion2.mX * quaternion2.mX) + (quaternion2.mY * quaternion2.mY)) + (quaternion2.mZ * quaternion2.mZ)) + (quaternion2.mW * quaternion2.mW);
            float num5 = 1f / num14;
            float num4 = -quaternion2.mX * num5;
            float num3 = -quaternion2.mY * num5;
            float num2 = -quaternion2.mZ * num5;
            float num = quaternion2.mW * num5;
            float num13 = (y * num2) - (z * num3);
            float num12 = (z * num4) - (x * num2);
            float num11 = (x * num3) - (y * num4);
            float num10 = ((x * num4) + (y * num3)) + (z * num2);
            quaternion.mX = ((x * num) + (num4 * w)) + num13;
            quaternion.mY = ((y * num) + (num3 * w)) + num12;
            quaternion.mZ = ((z * num) + (num2 * w)) + num11;
            quaternion.mW = (w * num) - num10;
            return quaternion;
        }
        
        public static bool operator ==(Quaternion quaternion1, Quaternion quaternion2)
        {
            return ((((quaternion1.mX == quaternion2.mX) && (quaternion1.mY == quaternion2.mY)) && (quaternion1.mZ == quaternion2.mZ)) && (quaternion1.mW == quaternion2.mW));
        }
        
        public static bool operator !=(Quaternion quaternion1, Quaternion quaternion2)
        {
            if (((quaternion1.mX == quaternion2.mX) && (quaternion1.mY == quaternion2.mY)) && (quaternion1.mZ == quaternion2.mZ))            
                return (quaternion1.mW != quaternion2.mW);            
            return true;
        }
        
        public static Quaternion operator *(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            float x = quaternion1.mX;
            float y = quaternion1.mY;
            float z = quaternion1.mZ;
            float w = quaternion1.mW;
            float num4 = quaternion2.mX;
            float num3 = quaternion2.mY;
            float num2 = quaternion2.mZ;
            float num = quaternion2.mW;
            float num12 = (y * num2) - (z * num3);
            float num11 = (z * num4) - (x * num2);
            float num10 = (x * num3) - (y * num4);
            float num9 = ((x * num4) + (y * num3)) + (z * num2);
            quaternion.mX = ((x * num) + (num4 * w)) + num12;
            quaternion.mY = ((y * num) + (num3 * w)) + num11;
            quaternion.mZ = ((z * num) + (num2 * w)) + num10;
            quaternion.mW = (w * num) - num9;
            return quaternion;
        }
        
        public static Quaternion operator *(Quaternion quaternion1, float scaleFactor)
        {
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX * scaleFactor;
            quaternion.mY = quaternion1.mY * scaleFactor;
            quaternion.mZ = quaternion1.mZ * scaleFactor;
            quaternion.mW = quaternion1.mW * scaleFactor;
            return quaternion;
        }
        
        public static Quaternion operator -(Quaternion quaternion1, Quaternion quaternion2)
        {
            Quaternion quaternion;
            quaternion.mX = quaternion1.mX - quaternion2.mX;
            quaternion.mY = quaternion1.mY - quaternion2.mY;
            quaternion.mZ = quaternion1.mZ - quaternion2.mZ;
            quaternion.mW = quaternion1.mW - quaternion2.mW;
            return quaternion;
        }
        
        public static Quaternion operator -(Quaternion quaternion)
        {
            Quaternion quaternion2;
            quaternion2.mX = -quaternion.mX;
            quaternion2.mY = -quaternion.mY;
            quaternion2.mZ = -quaternion.mZ;
            quaternion2.mW = -quaternion.mW;
            return quaternion2;
        }
        
        public override void ToString(String outStr)
        {
            ThrowUnimplemented();
        }

        internal Matrix4 ToMatrix()
        {
            Matrix4 matrix = Matrix4.Identity;
            ToMatrix(out matrix);
            return matrix;
        }

        /*internal void ToMatrix(out Matrix4 matrix)
        {
            Quaternion.ToMatrix(this, out matrix);
        }*/

        public void ToMatrix(out Matrix4 matrix)
        {            
            float fTx = mX + mX;
            float fTy = mY + mY;
            float fTz = mZ + mZ;
            float fTwx = fTx * mW;
            float fTwy = fTy * mW;
            float fTwz = fTz * mW;
            float fTxx = fTx * mX;
            float fTxy = fTy * mX;
            float fTxz = fTz * mX;
            float fTyy = fTy * mY;
            float fTyz = fTz * mY;
            float fTzz = fTz * mZ;

            matrix.m00 = 1.0f - (fTyy + fTzz);
            matrix.m01 = fTxy - fTwz;
            matrix.m02 = fTxz + fTwy;
            matrix.m03 = 0;

            matrix.m10 = fTxy + fTwz;
            matrix.m11 = 1.0f - (fTxx + fTzz);
            matrix.m12 = fTyz - fTwx;
            matrix.m13 = 0;

            matrix.m20 = fTxz - fTwy;
            matrix.m21 = fTyz + fTwx;
            matrix.m22 = 1.0f - (fTxx + fTyy);
            matrix.m23 = 0;

            matrix.m30 = 0;
            matrix.m31 = 0;
            matrix.m32 = 0;
            matrix.m33 = 1.0f;
        }

        internal Vector3 XYZ
        {
            get
            {
                return Vector3(mX, mY, mZ);
            }

            set mut
            {
                mX = value.mX;
                mY = value.mY;
                mZ = value.mZ;
            }
        }
    }
}