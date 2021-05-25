// This file contains portions of code from the FNA project (github.com/FNA-XNA/FNA),
// released under the Microsoft Public License

using System;

namespace Beefy.geom
{
	public struct Vector4 : IHashable
	{
		#region Public Static Properties

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 0, 0, 0, 0.
		/// </summary>
		public static Vector4 Zero
		{
			get
			{
				return zero;
			}
		}

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 1, 1, 1, 1.
		/// </summary>
		public static Vector4 One
		{
			get
			{
				return unit;
			}
		}

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 1, 0, 0, 0.
		/// </summary>
		public static Vector4 UnitX
		{
			get
			{
				return unitX;
			}
		}

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 0, 1, 0, 0.
		/// </summary>
		public static Vector4 UnitY
		{
			get
			{
				return unitY;
			}
		}

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 0, 0, 1, 0.
		/// </summary>
		public static Vector4 UnitZ
		{
			get
			{
				return unitZ;
			}
		}

		/// <summary>
		/// Returns a <see cref="Vector4"/> with components 0, 0, 0, 1.
		/// </summary>
		public static Vector4 UnitW
		{
			get
			{
				return unitW;
			}
		}

		/// <summary>
		/// The x coordinate of this <see cref="Vector4"/>.
		/// </summary>
		public float mX;

		/// <summary>
		/// The y coordinate of this <see cref="Vector4"/>.
		/// </summary>
		public float mY;

		/// <summary>
		/// The z coordinate of this <see cref="Vector4"/>.
		/// </summary>
		public float mZ;

		/// <summary>
		/// The w coordinate of this <see cref="Vector4"/>.
		/// </summary>
		public float mW;

		#endregion

		#region Private Static Fields

		private static Vector4 zero = .(); // Not readonly for performance -flibit
		private static readonly Vector4 unit = .(1f, 1f, 1f, 1f);
		private static readonly Vector4 unitX = .(1f, 0f, 0f, 0f);
		private static readonly Vector4 unitY = .(0f, 1f, 0f, 0f);
		private static readonly Vector4 unitZ = .(0f, 0f, 1f, 0f);
		private static readonly Vector4 unitW = .(0f, 0f, 0f, 1f);

		#endregion

		#region Public Constructors

		public this()
		{
			mX = 0;
			mY = 0;
			mZ = 0;
			mW = 0;
		}

		/// <summary>
		/// Constructs a 3d vector with X, Y, Z and W from four values.
		/// </summary>
		/// <param name="x">The x coordinate in 4d-space.</param>
		/// <param name="y">The y coordinate in 4d-space.</param>
		/// <param name="z">The z coordinate in 4d-space.</param>
		/// <param name="w">The w coordinate in 4d-space.</param>
		public this(float x, float y, float z, float w)
		{
			this.mX = x;
			this.mY = y;
			this.mZ = z;
			this.mW = w;
		}

		/// <summary>
		/// Constructs a 3d vector with X and Z from <see cref="Vector2"/> and Z and W from the scalars.
		/// </summary>
		/// <param name="value">The x and y coordinates in 4d-space.</param>
		/// <param name="z">The z coordinate in 4d-space.</param>
		/// <param name="w">The w coordinate in 4d-space.</param>
		public this(Vector2 value, float z, float w)
		{
			this.mX = value.mX;
			this.mY = value.mY;
			this.mZ = z;
			this.mW = w;
		}

		/// <summary>
		/// Constructs a 3d vector with X, Y, Z from <see cref="Vector3"/> and W from a scalar.
		/// </summary>
		/// <param name="value">The x, y and z coordinates in 4d-space.</param>
		/// <param name="w">The w coordinate in 4d-space.</param>
		public this(Vector3 value, float w)
		{
			this.mX = value.mX;
			this.mY = value.mY;
			this.mZ = value.mZ;
			this.mW = w;
		}

		/// <summary>
		/// Constructs a 4d vector with X, Y, Z and W set to the same value.
		/// </summary>
		/// <param name="value">The x, y, z and w coordinates in 4d-space.</param>
		public this(float value)
		{
			this.mX = value;
			this.mY = value;
			this.mZ = value;
			this.mW = value;
		}

		#endregion

		#region Public Methods

		/// <summary>
		/// Compares whether current instance is equal to specified <see cref="Vector4"/>.
		/// </summary>
		/// <param name="other">The <see cref="Vector4"/> to compare.</param>
		/// <returns><c>true</c> if the instances are equal; <c>false</c> otherwise.</returns>
		public bool Equals(Vector4 other)
		{
			return (	mX == other.mX &&
					mY == other.mY &&
					mZ == other.mZ &&
					mW == other.mW	);
		}

		/// <summary>
		/// Gets the hash code of this <see cref="Vector4"/>.
		/// </summary>
		/// <returns>Hash code of this <see cref="Vector4"/>.</returns>
		public int GetHashCode()
		{
			return mW.GetHashCode() + mX.GetHashCode() + mY.GetHashCode() + mZ.GetHashCode();
		}

		/// <summary>
		/// Returns the length of this <see cref="Vector4"/>.
		/// </summary>
		/// <returns>The length of this <see cref="Vector4"/>.</returns>
		public float Length()
		{
			return (float) Math.Sqrt((mX * mX) + (mY * mY) + (mZ * mZ) + (mW * mW));
		}

		/// <summary>
		/// Returns the squared length of this <see cref="Vector4"/>.
		/// </summary>
		/// <returns>The squared length of this <see cref="Vector4"/>.</returns>
		public float LengthSquared()
		{
			return (mX * mX) + (mY * mY) + (mZ * mZ) + (mW * mW);
		}

		/// <summary>
		/// Turns this <see cref="Vector4"/> to a unit vector with the same direction.
		/// </summary>
		public void Normalize() mut
		{
			float factor = 1.0f / (float) Math.Sqrt(
				(mX * mX) +
				(mY * mY) +
				(mZ * mZ) +
				(mW * mW)
			);
			mX *= factor;
			mY *= factor;
			mZ *= factor;
			mW *= factor;
		}

		public override void ToString(String str)
		{
			str.AppendF($"({mX}, {mY}, {mZ}, {mW})");
		}

		/// <summary>
		/// Performs vector addition on <paramref name="value1"/> and <paramref name="value2"/>.
		/// </summary>
		/// <param name="value1">The first vector to add.</param>
		/// <param name="value2">The second vector to add.</param>
		/// <returns>The result of the vector addition.</returns>
		public static Vector4 Add(Vector4 value1, Vector4 value2)
		{
			return .(
				value1.mX + value2.mX,
				value1.mY + value2.mY,
				value1.mZ + value2.mZ,
				value1.mW + value2.mW);
		}

		/// <summary>
		/// Performs vector addition on <paramref name="value1"/> and
		/// <paramref name="value2"/>, storing the result of the
		/// addition in <paramref name="result"/>.
		/// </summary>
		/// <param name="value1">The first vector to add.</param>
		/// <param name="value2">The second vector to add.</param>
		/// <param name="result">The result of the vector addition.</param>
		public static void Add(ref Vector4 value1, ref Vector4 value2, out Vector4 result)
		{
			result.mW = value1.mW + value2.mW;
			result.mX = value1.mX + value2.mX;
			result.mY = value1.mY + value2.mY;
			result.mZ = value1.mZ + value2.mZ;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains the cartesian coordinates of a vector specified in barycentric coordinates and relative to 4d-triangle.
		/// </summary>
		/// <param name="value1">The first vector of 4d-triangle.</param>
		/// <param name="value2">The second vector of 4d-triangle.</param>
		/// <param name="value3">The third vector of 4d-triangle.</param>
		/// <param name="amount1">Barycentric scalar <c>b2</c> which represents a weighting factor towards second vector of 4d-triangle.</param>
		/// <param name="amount2">Barycentric scalar <c>b3</c> which represents a weighting factor towards third vector of 4d-triangle.</param>
		/// <returns>The cartesian translation of barycentric coordinates.</returns>
		public static Vector4 Barycentric(
			Vector4 value1,
			Vector4 value2,
			Vector4 value3,
			float amount1,
			float amount2
		) {
			return Vector4(
				Math.Barycentric(value1.mX, value2.mX, value3.mX, amount1, amount2),
				Math.Barycentric(value1.mY, value2.mY, value3.mY, amount1, amount2),
				Math.Barycentric(value1.mZ, value2.mZ, value3.mZ, amount1, amount2),
				Math.Barycentric(value1.mW, value2.mW, value3.mW, amount1, amount2)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains the cartesian coordinates of a vector specified in barycentric coordinates and relative to 4d-triangle.
		/// </summary>
		/// <param name="value1">The first vector of 4d-triangle.</param>
		/// <param name="value2">The second vector of 4d-triangle.</param>
		/// <param name="value3">The third vector of 4d-triangle.</param>
		/// <param name="amount1">Barycentric scalar <c>b2</c> which represents a weighting factor towards second vector of 4d-triangle.</param>
		/// <param name="amount2">Barycentric scalar <c>b3</c> which represents a weighting factor towards third vector of 4d-triangle.</param>
		/// <param name="result">The cartesian translation of barycentric coordinates as an output parameter.</param>
		public static void Barycentric(
			ref Vector4 value1,
			ref Vector4 value2,
			ref Vector4 value3,
			float amount1,
			float amount2,
			out Vector4 result
		) {
			result.mX = Math.Barycentric(value1.mX, value2.mX, value3.mX, amount1, amount2);
			result.mY = Math.Barycentric(value1.mY, value2.mY, value3.mY, amount1, amount2);
			result.mZ = Math.Barycentric(value1.mZ, value2.mZ, value3.mZ, amount1, amount2);
			result.mW = Math.Barycentric(value1.mW, value2.mW, value3.mW, amount1, amount2);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains CatmullRom interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">The first vector in interpolation.</param>
		/// <param name="value2">The second vector in interpolation.</param>
		/// <param name="value3">The third vector in interpolation.</param>
		/// <param name="value4">The fourth vector in interpolation.</param>
		/// <param name="amount">Weighting factor.</param>
		/// <returns>The result of CatmullRom interpolation.</returns>
		public static Vector4 CatmullRom(
			Vector4 value1,
			Vector4 value2,
			Vector4 value3,
			Vector4 value4,
			float amount
		) {
			return Vector4(
				Math.CatmullRom(value1.mX, value2.mX, value3.mX, value4.mX, amount),
				Math.CatmullRom(value1.mY, value2.mY, value3.mY, value4.mY, amount),
				Math.CatmullRom(value1.mZ, value2.mZ, value3.mZ, value4.mZ, amount),
				Math.CatmullRom(value1.mW, value2.mW, value3.mW, value4.mW, amount)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains CatmullRom interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">The first vector in interpolation.</param>
		/// <param name="value2">The second vector in interpolation.</param>
		/// <param name="value3">The third vector in interpolation.</param>
		/// <param name="value4">The fourth vector in interpolation.</param>
		/// <param name="amount">Weighting factor.</param>
		/// <param name="result">The result of CatmullRom interpolation as an output parameter.</param>
		public static void CatmullRom(
			ref Vector4 value1,
			ref Vector4 value2,
			ref Vector4 value3,
			ref Vector4 value4,
			float amount,
			out Vector4 result
		) {
			result.mX = Math.CatmullRom(value1.mX, value2.mX, value3.mX, value4.mX, amount);
			result.mY = Math.CatmullRom(value1.mY, value2.mY, value3.mY, value4.mY, amount);
			result.mZ = Math.CatmullRom(value1.mZ, value2.mZ, value3.mZ, value4.mZ, amount);
			result.mW = Math.CatmullRom(value1.mW, value2.mW, value3.mW, value4.mW, amount);
		}

		/// <summary>
		/// Clamps the specified value within a range.
		/// </summary>
		/// <param name="value1">The value to clamp.</param>
		/// <param name="min">The min value.</param>
		/// <param name="max">The max value.</param>
		/// <returns>The clamped value.</returns>
		public static Vector4 Clamp(Vector4 value1, Vector4 min, Vector4 max)
		{
			return Vector4(
				Math.Clamp(value1.mX, min.mX, max.mX),
				Math.Clamp(value1.mY, min.mY, max.mY),
				Math.Clamp(value1.mZ, min.mZ, max.mZ),
				Math.Clamp(value1.mW, min.mW, max.mW)
			);
		}

		/// <summary>
		/// Clamps the specified value within a range.
		/// </summary>
		/// <param name="value1">The value to clamp.</param>
		/// <param name="min">The min value.</param>
		/// <param name="max">The max value.</param>
		/// <param name="result">The clamped value as an output parameter.</param>
		public static void Clamp(
			Vector4 value1,
			Vector4 min,
			Vector4 max,
			out Vector4 result
		) {
			result.mX = Math.Clamp(value1.mX, min.mX, max.mX);
			result.mY = Math.Clamp(value1.mY, min.mY, max.mY);
			result.mZ = Math.Clamp(value1.mZ, min.mZ, max.mZ);
			result.mW = Math.Clamp(value1.mW, min.mW, max.mW);
		}

		/// <summary>
		/// Returns the distance between two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <returns>The distance between two vectors.</returns>
		public static float Distance(Vector4 value1, Vector4 value2)
		{
			return (float) Math.Sqrt(DistanceSquared(value1, value2));
		}

		/// <summary>
		/// Returns the distance between two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="result">The distance between two vectors as an output parameter.</param>
		public static void Distance(ref Vector4 value1, ref Vector4 value2, out float result)
		{
			result = (float) Math.Sqrt(DistanceSquared(value1, value2));
		}

		/// <summary>
		/// Returns the squared distance between two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <returns>The squared distance between two vectors.</returns>
		public static float DistanceSquared(Vector4 value1, Vector4 value2)
		{
			return (
				(value1.mW - value2.mW) * (value1.mW - value2.mW) +
				(value1.mX - value2.mX) * (value1.mX - value2.mX) +
				(value1.mY - value2.mY) * (value1.mY - value2.mY) +
				(value1.mZ - value2.mZ) * (value1.mZ - value2.mZ)
			);
		}

		/// <summary>
		/// Returns the squared distance between two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="result">The squared distance between two vectors as an output parameter.</param>
		public static void DistanceSquared(
			ref Vector4 value1,
			ref Vector4 value2,
			out float result
		) {
			result = (
				(value1.mW - value2.mW) * (value1.mW - value2.mW) +
				(value1.mX - value2.mX) * (value1.mX - value2.mX) +
				(value1.mY - value2.mY) * (value1.mY - value2.mY) +
				(value1.mZ - value2.mZ) * (value1.mZ - value2.mZ)
			);
		}

		/// <summary>
		/// Divides the components of a <see cref="Vector4"/> by the components of another <see cref="Vector4"/>.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Divisor <see cref="Vector4"/>.</param>
		/// <returns>The result of dividing the vectors.</returns>
		public static Vector4 Divide(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW /= value2.mW;
			value1.mX /= value2.mX;
			value1.mY /= value2.mY;
			value1.mZ /= value2.mZ;
			return value1;
		}

		/// <summary>
		/// Divides the components of a <see cref="Vector4"/> by a scalar.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="divider">Divisor scalar.</param>
		/// <returns>The result of dividing a vector by a scalar.</returns>
		public static Vector4 Divide(Vector4 value1, float divider)
		{
			var value1;
			float factor = 1f / divider;
			value1.mW *= factor;
			value1.mX *= factor;
			value1.mY *= factor;
			value1.mZ *= factor;
			return value1;
		}

		/// <summary>
		/// Divides the components of a <see cref="Vector4"/> by a scalar.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="divider">Divisor scalar.</param>
		/// <param name="result">The result of dividing a vector by a scalar as an output parameter.</param>
		public static void Divide(ref Vector4 value1, float divider, out Vector4 result)
		{
			float factor = 1f / divider;
			result.mW = value1.mW * factor;
			result.mX = value1.mX * factor;
			result.mY = value1.mY * factor;
			result.mZ = value1.mZ * factor;
		}

		/// <summary>
		/// Divides the components of a <see cref="Vector4"/> by the components of another <see cref="Vector4"/>.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Divisor <see cref="Vector4"/>.</param>
		/// <param name="result">The result of dividing the vectors as an output parameter.</param>
		public static void Divide(
			ref Vector4 value1,
			ref Vector4 value2,
			out Vector4 result
		) {
			result.mW = value1.mW / value2.mW;
			result.mX = value1.mX / value2.mX;
			result.mY = value1.mY / value2.mY;
			result.mZ = value1.mZ / value2.mZ;
		}

		/// <summary>
		/// Returns a dot product of two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <returns>The dot product of two vectors.</returns>
		public static float Dot(Vector4 vector1, Vector4 vector2)
		{
			return (
				vector1.mX * vector2.mX +
				vector1.mY * vector2.mY +
				vector1.mZ * vector2.mZ +
				vector1.mW * vector2.mW
			);
		}

		/// <summary>
		/// Returns a dot product of two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="result">The dot product of two vectors as an output parameter.</param>
		public static void Dot(ref Vector4 vector1, ref Vector4 vector2, out float result)
		{
			result = (
				(vector1.mX * vector2.mX) +
				(vector1.mY * vector2.mY) +
				(vector1.mZ * vector2.mZ) +
				(vector1.mW * vector2.mW)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains hermite spline interpolation.
		/// </summary>
		/// <param name="value1">The first position vector.</param>
		/// <param name="tangent1">The first tangent vector.</param>
		/// <param name="value2">The second position vector.</param>
		/// <param name="tangent2">The second tangent vector.</param>
		/// <param name="amount">Weighting factor.</param>
		/// <returns>The hermite spline interpolation vector.</returns>
		public static Vector4 Hermite(
			Vector4 value1,
			Vector4 tangent1,
			Vector4 value2,
			Vector4 tangent2,
			float amount
		) {
			return Vector4(
				Math.Hermite(value1.mX, tangent1.mX, value2.mX, tangent2.mX, amount),
				Math.Hermite(value1.mY, tangent1.mY, value2.mY, tangent2.mY, amount),
				Math.Hermite(value1.mZ, tangent1.mZ, value2.mZ, tangent2.mZ, amount),
				Math.Hermite(value1.mW, tangent1.mW, value2.mW, tangent2.mW, amount)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains hermite spline interpolation.
		/// </summary>
		/// <param name="value1">The first position vector.</param>
		/// <param name="tangent1">The first tangent vector.</param>
		/// <param name="value2">The second position vector.</param>
		/// <param name="tangent2">The second tangent vector.</param>
		/// <param name="amount">Weighting factor.</param>
		/// <param name="result">The hermite spline interpolation vector as an output parameter.</param>
		public static void Hermite(
			Vector4 value1,
			Vector4 tangent1,
			Vector4 value2,
			Vector4 tangent2,
			float amount,
			out Vector4 result
		) {
			result.mW = Math.Hermite(value1.mW, tangent1.mW, value2.mW, tangent2.mW, amount);
			result.mX = Math.Hermite(value1.mX, tangent1.mX, value2.mX, tangent2.mX, amount);
			result.mY = Math.Hermite(value1.mY, tangent1.mY, value2.mY, tangent2.mY, amount);
			result.mZ = Math.Hermite(value1.mZ, tangent1.mZ, value2.mZ, tangent2.mZ, amount);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains linear interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="amount">Weighting value(between 0.0 and 1.0).</param>
		/// <returns>The result of linear interpolation of the specified vectors.</returns>
		public static Vector4 Lerp(Vector4 value1, Vector4 value2, float amount)
		{
			return Vector4(
				Math.Lerp(value1.mX, value2.mX, amount),
				Math.Lerp(value1.mY, value2.mY, amount),
				Math.Lerp(value1.mZ, value2.mZ, amount),
				Math.Lerp(value1.mW, value2.mW, amount)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains linear interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="amount">Weighting value(between 0.0 and 1.0).</param>
		/// <param name="result">The result of linear interpolation of the specified vectors as an output parameter.</param>
		public static void Lerp(
			ref Vector4 value1,
			ref Vector4 value2,
			float amount,
			out Vector4 result
		) {
			result.mX = Math.Lerp(value1.mX, value2.mX, amount);
			result.mY = Math.Lerp(value1.mY, value2.mY, amount);
			result.mZ = Math.Lerp(value1.mZ, value2.mZ, amount);
			result.mW = Math.Lerp(value1.mW, value2.mW, amount);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a maximal values from the two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <returns>The <see cref="Vector4"/> with maximal values from the two vectors.</returns>
		public static Vector4 Max(Vector4 value1, Vector4 value2)
		{
			return Vector4(
				Math.Max(value1.mX, value2.mX),
				Math.Max(value1.mY, value2.mY),
				Math.Max(value1.mZ, value2.mZ),
				Math.Max(value1.mW, value2.mW)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a maximal values from the two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="result">The <see cref="Vector4"/> with maximal values from the two vectors as an output parameter.</param>
		public static void Max(ref Vector4 value1, ref Vector4 value2, out Vector4 result)
		{
			result.mX = Math.Max(value1.mX, value2.mX);
			result.mY = Math.Max(value1.mY, value2.mY);
			result.mZ = Math.Max(value1.mZ, value2.mZ);
			result.mW = Math.Max(value1.mW, value2.mW);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a minimal values from the two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <returns>The <see cref="Vector4"/> with minimal values from the two vectors.</returns>
		public static Vector4 Min(Vector4 value1, Vector4 value2)
		{
			return Vector4(
				Math.Min(value1.mX, value2.mX),
				Math.Min(value1.mY, value2.mY),
				Math.Min(value1.mZ, value2.mZ),
				Math.Min(value1.mW, value2.mW)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a minimal values from the two vectors.
		/// </summary>
		/// <param name="value1">The first vector.</param>
		/// <param name="value2">The second vector.</param>
		/// <param name="result">The <see cref="Vector4"/> with minimal values from the two vectors as an output parameter.</param>
		public static void Min(ref Vector4 value1, ref Vector4 value2, out Vector4 result)
		{
			result.mX = Math.Min(value1.mX, value2.mX);
			result.mY = Math.Min(value1.mY, value2.mY);
			result.mZ = Math.Min(value1.mZ, value2.mZ);
			result.mW = Math.Min(value1.mW, value2.mW);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a multiplication of two vectors.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <returns>The result of the vector multiplication.</returns>
		public static Vector4 Multiply(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW *= value2.mW;
			value1.mX *= value2.mX;
			value1.mY *= value2.mY;
			value1.mZ *= value2.mZ;
			return value1;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a multiplication of <see cref="Vector4"/> and a scalar.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="scaleFactor">Scalar value.</param>
		/// <returns>The result of the vector multiplication with a scalar.</returns>
		public static Vector4 Multiply(Vector4 value1, float scaleFactor)
		{
			var value1;
			value1.mW *= scaleFactor;
			value1.mX *= scaleFactor;
			value1.mY *= scaleFactor;
			value1.mZ *= scaleFactor;
			return value1;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a multiplication of <see cref="Vector4"/> and a scalar.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="scaleFactor">Scalar value.</param>
		/// <param name="result">The result of the multiplication with a scalar as an output parameter.</param>
		public static void Multiply(Vector4 value1, float scaleFactor, out Vector4 result)
		{
			result.mW = value1.mW * scaleFactor;
			result.mX = value1.mX * scaleFactor;
			result.mY = value1.mY * scaleFactor;
			result.mZ = value1.mZ * scaleFactor;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a multiplication of two vectors.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <param name="result">The result of the vector multiplication as an output parameter.</param>
		public static void Multiply(ref Vector4 value1, ref Vector4 value2, out Vector4 result)
		{
			result.mW = value1.mW * value2.mW;
			result.mX = value1.mX * value2.mX;
			result.mY = value1.mY * value2.mY;
			result.mZ = value1.mZ * value2.mZ;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains the specified vector inversion.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <returns>The result of the vector inversion.</returns>
		public static Vector4 Negate(Vector4 value)
		{
			return Vector4(-value.mX, -value.mY, -value.mZ, -value.mW);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains the specified vector inversion.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="result">The result of the vector inversion as an output parameter.</param>
		public static void Negate(ref Vector4 value, out Vector4 result)
		{
			result.mX = -value.mX;
			result.mY = -value.mY;
			result.mZ = -value.mZ;
			result.mW = -value.mW;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a normalized values from another vector.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <returns>Unit vector.</returns>
		public static Vector4 Normalize(Vector4 vector)
		{
			float factor = 1.0f / (float) Math.Sqrt(
				(vector.mX * vector.mX) +
				(vector.mY * vector.mY) +
				(vector.mZ * vector.mZ) +
				(vector.mW * vector.mW)
			);
			return Vector4(
				vector.mX * factor,
				vector.mY * factor,
				vector.mZ * factor,
				vector.mW * factor
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a normalized values from another vector.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="result">Unit vector as an output parameter.</param>
		public static void Normalize(Vector4 vector, out Vector4 result)
		{
			float factor = 1.0f / (float) Math.Sqrt(
				(vector.mX * vector.mX) +
				(vector.mY * vector.mY) +
				(vector.mZ * vector.mZ) +
				(vector.mW * vector.mW)
			);
			result.mX = vector.mX * factor;
			result.mY = vector.mY * factor;
			result.mZ = vector.mZ * factor;
			result.mW = vector.mW * factor;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains cubic interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <param name="amount">Weighting value.</param>
		/// <returns>Cubic interpolation of the specified vectors.</returns>
		public static Vector4 SmoothStep(Vector4 value1, Vector4 value2, float amount)
		{
			return Vector4(
				Math.SmoothStep(value1.mX, value2.mX, amount),
				Math.SmoothStep(value1.mY, value2.mY, amount),
				Math.SmoothStep(value1.mZ, value2.mZ, amount),
				Math.SmoothStep(value1.mW, value2.mW, amount)
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains cubic interpolation of the specified vectors.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <param name="amount">Weighting value.</param>
		/// <param name="result">Cubic interpolation of the specified vectors as an output parameter.</param>
		public static void SmoothStep(
			Vector4 value1,
			Vector4 value2,
			float amount,
			out Vector4 result
		) {
			result.mX = Math.SmoothStep(value1.mX, value2.mX, amount);
			result.mY = Math.SmoothStep(value1.mY, value2.mY, amount);
			result.mZ = Math.SmoothStep(value1.mZ, value2.mZ, amount);
			result.mW = Math.SmoothStep(value1.mW, value2.mW, amount);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains subtraction of on <see cref="Vector4"/> from a another.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <returns>The result of the vector subtraction.</returns>
		public static Vector4 Subtract(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW -= value2.mW;
			value1.mX -= value2.mX;
			value1.mY -= value2.mY;
			value1.mZ -= value2.mZ;
			return value1;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains subtraction of on <see cref="Vector4"/> from a another.
		/// </summary>
		/// <param name="value1">Source <see cref="Vector4"/>.</param>
		/// <param name="value2">Source <see cref="Vector4"/>.</param>
		/// <param name="result">The result of the vector subtraction as an output parameter.</param>
		public static void Subtract(Vector4 value1, Vector4 value2, out Vector4 result)
		{
			result.mW = value1.mW - value2.mW;
			result.mX = value1.mX - value2.mX;
			result.mY = value1.mY - value2.mY;
			result.mZ = value1.mZ - value2.mZ;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 2d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector2"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector2 position, Matrix4 matrix)
		{
			Vector4 result;
			Transform(position, matrix, out result);
			return result;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 3d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector3"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector3 position, Matrix4 matrix)
		{
			Vector4 result;
			Transform(position, matrix, out result);
			return result;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 4d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector4 vector, Matrix4 matrix)
		{
			return Transform(vector, matrix);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 2d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector2"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(Vector2 position, Matrix4 matrix, out Vector4 result)
		{
			result = Vector4(
				(position.mX * matrix.m00) + (position.mY * matrix.m01) + matrix.m03,
				(position.mX * matrix.m10) + (position.mY * matrix.m11) + matrix.m13,
				(position.mX * matrix.m20) + (position.mY * matrix.m21) + matrix.m23,
				(position.mX * matrix.m30) + (position.mY * matrix.m31) + matrix.m33
			);
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 3d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector3"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(Vector3 position, Matrix4 matrix, out Vector4 result)
		{
			float x = (
				(position.mX * matrix.m00) +
				(position.mY * matrix.m01) +
				(position.mZ * matrix.m02) +
				matrix.m03
			);
			float y = (
				(position.mX * matrix.m10) +
				(position.mY * matrix.m11) +
				(position.mZ * matrix.m12) +
				matrix.m13
			);
			float z = (
				(position.mX * matrix.m20) +
				(position.mY * matrix.m21) +
				(position.mZ * matrix.m22) +
				matrix.m23
			);
			float w = (
				(position.mX * matrix.m30) +
				(position.mY * matrix.m31) +
				(position.mZ * matrix.m32) +
				matrix.m33
			);
			result.mX = x;
			result.mY = y;
			result.mZ = z;
			result.mW = w;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 4d-vector by the specified <see cref="Matrix"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(Vector4 vector, Matrix4 matrix, out Vector4 result)
		{
			float x = (
				(vector.mX * matrix.m00) +
				(vector.mY * matrix.m01) +
				(vector.mZ * matrix.m02) +
				(vector.mW * matrix.m03)
			);
			float y = (
				(vector.mX * matrix.m10) +
				(vector.mY * matrix.m11) +
				(vector.mZ * matrix.m12) +
				(vector.mW * matrix.m13)
			);
			float z = (
				(vector.mX * matrix.m20) +
				(vector.mY * matrix.m21) +
				(vector.mZ * matrix.m22) +
				(vector.mW * matrix.m23)
			);
			float w = (
				(vector.mX * matrix.m30) +
				(vector.mY * matrix.m31) +
				(vector.mZ * matrix.m32) +
				(vector.mW * matrix.m33)
			);
			result.mX = x;
			result.mY = y;
			result.mZ = z;
			result.mW = w;
		}

		/// <summary>
		/// Apply transformation on all vectors within array of <see cref="Vector4"/> by the specified <see cref="Matrix"/> and places the results in an another array.
		/// </summary>
		/// <param name="sourceArray">Source array.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <param name="destinationArray">Destination array.</param>
		public static void Transform(
			Vector4[] sourceArray,
			Matrix4 matrix,
			Vector4[] destinationArray
		) {
			if (sourceArray == null)
			{
				Runtime.FatalError("sourceArray");
			}
			if (destinationArray == null)
			{
				Runtime.FatalError("destinationArray");
			}
			if (destinationArray.Count < sourceArray.Count)
			{
				Runtime.FatalError(
					"destinationArray is too small to contain the result."
				);
			}
			for (int i = 0; i < sourceArray.Count; i += 1)
			{
				Transform(
					sourceArray[i],
					matrix,
					out destinationArray[i]
				);
			}
		}

		/// <summary>
		/// Apply transformation on vectors within array of <see cref="Vector4"/> by the specified <see cref="Matrix"/> and places the results in an another array.
		/// </summary>
		/// <param name="sourceArray">Source array.</param>
		/// <param name="sourceIndex">The starting index of transformation in the source array.</param>
		/// <param name="matrix">The transformation <see cref="Matrix"/>.</param>
		/// <param name="destinationArray">Destination array.</param>
		/// <param name="destinationIndex">The starting index in the destination array, where the first <see cref="Vector4"/> should be written.</param>
		/// <param name="length">The number of vectors to be transformed.</param>
		public static void Transform(
			Vector4[] sourceArray,
			int sourceIndex,
			Matrix4 matrix,
			Vector4[] destinationArray,
			int destinationIndex,
			int length
		) {
			if (sourceArray == null)
			{
				Runtime.FatalError("sourceArray");
			}
			if (destinationArray == null)
			{
				Runtime.FatalError("destinationArray");
			}
			if (destinationIndex + length > destinationArray.Count)
			{
				Runtime.FatalError(
					"destinationArray is too small to contain the result."
				);
			}
			if (sourceIndex + length > sourceArray.Count)
			{
				Runtime.FatalError(
					"The combination of sourceIndex and length was greater than sourceArray.Length."
				);
			}
			for (int i = 0; i < length; i += 1)
			{
				Transform(
					sourceArray[i + sourceIndex],
					matrix,
					out destinationArray[i + destinationIndex]
				);
			}
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 2d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector2"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector2 value, Quaternion rotation)
		{
			Vector4 result;
			Transform(value, rotation, out result);
			return result;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 3d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector3"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector3 value, Quaternion rotation)
		{
			Vector4 result;
			Transform(value, rotation, out result);
			return result;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 4d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <returns>Transformed <see cref="Vector4"/>.</returns>
		public static Vector4 Transform(Vector4 value, Quaternion rotation)
		{
			Vector4 result;
			Transform(value, rotation, out result);
			return result;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 2d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector2"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(
			Vector2 value,
			Quaternion rotation,
			out Vector4 result
		) {
			double xx = rotation.mX + rotation.mX;
			double yy = rotation.mY + rotation.mY;
			double zz = rotation.mZ + rotation.mZ;
			double wxx = rotation.mW * xx;
			double wyy = rotation.mW * yy;
			double wzz = rotation.mW * zz;
			double xxx = rotation.mX * xx;
			double xyy = rotation.mX * yy;
			double xzz = rotation.mX * zz;
			double yyy = rotation.mY * yy;
			double yzz = rotation.mY * zz;
			double zzz = rotation.mZ * zz;
			result.mX = (float) (
				(double) value.mX * (1.0 - yyy - zzz) +
				(double) value.mY * (xyy - wzz)
			);
			result.mY = (float) (
				(double) value.mX * (xyy + wzz) +
				(double) value.mY * (1.0 - xxx - zzz)
			);
			result.mZ = (float) (
				(double) value.mX * (xzz - wyy) +
				(double) value.mY * (yzz + wxx)
			);
			result.mW = 1.0f;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 3d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector3"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(
			Vector3 value,
			Quaternion rotation,
			out Vector4 result
		) {
			double xx = rotation.mX + rotation.mX;
			double yy = rotation.mY + rotation.mY;
			double zz = rotation.mZ + rotation.mZ;
			double wxx = rotation.mW * xx;
			double wyy = rotation.mW * yy;
			double wzz = rotation.mW * zz;
			double xxx = rotation.mX * xx;
			double xyy = rotation.mX * yy;
			double xzz = rotation.mX * zz;
			double yyy = rotation.mY * yy;
			double yzz = rotation.mY * zz;
			double zzz = rotation.mZ * zz;
			result.mX = (float) (
				(double) value.mX * (1.0 - yyy - zzz) +
				(double) value.mY * (xyy - wzz) +
				(double) value.mZ * (xzz + wyy)
			);
			result.mY = (float) (
				(double) value.mX * (xyy + wzz) +
				(double) value.mY * (1.0 - xxx - zzz) +
				(double) value.mZ * (yzz - wxx)
			);
			result.mZ = (float) (
				(double) value.mX * (xzz - wyy) +
				(double) value.mY * (yzz + wxx) +
				(double) value.mZ * (1.0 - xxx - yyy)
			);
			result.mW = 1.0f;
		}

		/// <summary>
		/// Creates a new <see cref="Vector4"/> that contains a transformation of 4d-vector by the specified <see cref="Quaternion"/>.
		/// </summary>
		/// <param name="value">Source <see cref="Vector4"/>.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <param name="result">Transformed <see cref="Vector4"/> as an output parameter.</param>
		public static void Transform(
			Vector4 value,
			Quaternion rotation,
			out Vector4 result
		) {
			double xx = rotation.mX + rotation.mX;
			double yy = rotation.mY + rotation.mY;
			double zz = rotation.mZ + rotation.mZ;
			double wxx = rotation.mW * xx;
			double wyy = rotation.mW * yy;
			double wzz = rotation.mW * zz;
			double xxx = rotation.mX * xx;
			double xyy = rotation.mX * yy;
			double xzz = rotation.mX * zz;
			double yyy = rotation.mY * yy;
			double yzz = rotation.mY * zz;
			double zzz = rotation.mZ * zz;
			result.mX = (float) (
				(double) value.mX * (1.0 - yyy - zzz) +
				(double) value.mY * (xyy - wzz) +
				(double) value.mZ * (xzz + wyy)
			);
			result.mY = (float) (
				(double) value.mX * (xyy + wzz) +
				(double) value.mY * (1.0 - xxx - zzz) +
				(double) value.mZ * (yzz - wxx)
			);
			result.mZ = (float) (
				(double) value.mX * (xzz - wyy) +
				(double) value.mY * (yzz + wxx) +
				(double) value.mZ * (1.0 - xxx - yyy)
			);
			result.mW = value.mW;
		}

		/// <summary>
		/// Apply transformation on all vectors within array of <see cref="Vector4"/> by the specified <see cref="Quaternion"/> and places the results in an another array.
		/// </summary>
		/// <param name="sourceArray">Source array.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <param name="destinationArray">Destination array.</param>
		public static void Transform(
			Vector4[] sourceArray,
			Quaternion rotation,
			Vector4[] destinationArray
		) {
			if (sourceArray == null)
			{
				Runtime.FatalError("sourceArray");
			}
			if (destinationArray == null)
			{
				Runtime.FatalError("destinationArray");
			}
			if (destinationArray.Count < sourceArray.Count)
			{
				Runtime.FatalError(
					"destinationArray is too small to contain the result."
				);
			}
			for (int i = 0; i < sourceArray.Count; i += 1)
			{
				Transform(
					sourceArray[i],
					rotation,
					out destinationArray[i]
				);
			}
		}

		/// <summary>
		/// Apply transformation on vectors within array of <see cref="Vector4"/> by the specified <see cref="Quaternion"/> and places the results in an another array.
		/// </summary>
		/// <param name="sourceArray">Source array.</param>
		/// <param name="sourceIndex">The starting index of transformation in the source array.</param>
		/// <param name="rotation">The <see cref="Quaternion"/> which contains rotation transformation.</param>
		/// <param name="destinationArray">Destination array.</param>
		/// <param name="destinationIndex">The starting index in the destination array, where the first <see cref="Vector4"/> should be written.</param>
		/// <param name="length">The number of vectors to be transformed.</param>
		public static void Transform(
			Vector4[] sourceArray,
			int sourceIndex,
			Quaternion rotation,
			Vector4[] destinationArray,
			int destinationIndex,
			int length
		) {
			if (sourceArray == null)
			{
				Runtime.FatalError("sourceArray");
			}
			if (destinationArray == null)
			{
				Runtime.FatalError("destinationArray");
			}
			if (destinationIndex + length > destinationArray.Count)
			{
				Runtime.FatalError(
					"destinationArray is too small to contain the result."
				);
			}
			if (sourceIndex + length > sourceArray.Count)
			{
				Runtime.FatalError(
					"The combination of sourceIndex and length was greater than sourceArray.Length."
				);
			}
			for (int i = 0; i < length; i += 1)
			{
				Transform(
					sourceArray[i + sourceIndex],
					rotation,
					out destinationArray[i + destinationIndex]
				);
			}
		}

		#endregion

		#region Public Static Operators

		public static Vector4 operator -(Vector4 value)
		{
			return Vector4(-value.mX, -value.mY, -value.mZ, -value.mW);
		}

		public static bool operator ==(Vector4 value1, Vector4 value2)
		{
			return (	value1.mX == value2.mX &&
					value1.mY == value2.mY &&
					value1.mZ == value2.mZ &&
					value1.mW == value2.mW	);
		}

		public static bool operator !=(Vector4 value1, Vector4 value2)
		{
			return !(value1 == value2);
		}

		public static Vector4 operator +(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW += value2.mW;
			value1.mX += value2.mX;
			value1.mY += value2.mY;
			value1.mZ += value2.mZ;
			return value1;
		}

		public static Vector4 operator -(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW -= value2.mW;
			value1.mX -= value2.mX;
			value1.mY -= value2.mY;
			value1.mZ -= value2.mZ;
			return value1;
		}

		public static Vector4 operator *(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW *= value2.mW;
			value1.mX *= value2.mX;
			value1.mY *= value2.mY;
			value1.mZ *= value2.mZ;
			return value1;
		}

		public static Vector4 operator *(Vector4 value1, float scaleFactor)
		{
			var value1;
			value1.mW *= scaleFactor;
			value1.mX *= scaleFactor;
			value1.mY *= scaleFactor;
			value1.mZ *= scaleFactor;
			return value1;
		}

		public static Vector4 operator *(float scaleFactor, Vector4 value1)
		{
			var value1;
			value1.mW *= scaleFactor;
			value1.mX *= scaleFactor;
			value1.mY *= scaleFactor;
			value1.mZ *= scaleFactor;
			return value1;
		}

		public static Vector4 operator /(Vector4 value1, Vector4 value2)
		{
			var value1;
			value1.mW /= value2.mW;
			value1.mX /= value2.mX;
			value1.mY /= value2.mY;
			value1.mZ /= value2.mZ;
			return value1;
		}

		public static Vector4 operator /(Vector4 value1, float divider)
		{
			var value1;
			float factor = 1f / divider;
			value1.mW *= factor;
			value1.mX *= factor;
			value1.mY *= factor;
			value1.mZ *= factor;
			return value1;
		}

		#endregion
	}
}
