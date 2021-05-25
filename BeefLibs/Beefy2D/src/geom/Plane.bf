// This file contains portions of code from the FNA project (github.com/FNA-XNA/FNA),
// released under the Microsoft Public License

using System;

namespace Beefy.geom
{
	/// <summary>
	/// Defines the intersection between a <see cref="Plane"/> and a bounding volume.
	/// </summary>
	public enum PlaneIntersectionType
	{
		/// <summary>
		/// There is no intersection, the bounding volume is in the negative half space of the plane.
		/// </summary>
		Front,
		/// <summary>
		/// There is no intersection, the bounding volume is in the positive half space of the plane.
		/// </summary>
		Back,
		/// <summary>
		/// The plane is intersected.
		/// </summary>
		Intersecting
	}

	public struct Plane
	{
		public Vector3 Normal;
		public float D;

		public this(Vector4 value)
			: this(Vector3(value.mX, value.mY, value.mZ), value.mW)
		{
		}

		public this(Vector3 normal, float d)
		{
			Normal = normal;
			D = d;
		}

		public this(Vector3 a, Vector3 b, Vector3 c)
		{
			Vector3 ab = b - a;
			Vector3 ac = c - a;

			Vector3 cross = Vector3.Cross(ab, ac);
			Vector3.Normalize(cross, out Normal);
			D = -(Vector3.Dot(Normal, a));
		}

		public this(float a, float b, float c, float d)
			: this(Vector3(a, b, c), d)
		{

		}

		public float Dot(Vector4 value)
		{
			return (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ) +
				(this.D * value.mW)
			);
		}

		public void Dot(ref Vector4 value, out float result)
		{
			result = (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ) +
				(this.D * value.mW)
			);
		}

		public float DotCoordinate(Vector3 value)
		{
			return (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ) +
				this.D
			);
		}

		public void DotCoordinate(ref Vector3 value, out float result)
		{
			result = (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ) +
				this.D
			);
		}

		public float DotNormal(Vector3 value)
		{
			return (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ)
			);
		}

		public void DotNormal(Vector3 value, out float result)
		{
			result = (
				(this.Normal.mX * value.mX) +
				(this.Normal.mY * value.mY) +
				(this.Normal.mZ * value.mZ)
			);
		}

		public void Normalize() mut
		{
			float length = Normal.Length;
			float factor = 1.0f / length;
			Normal = Vector3.Multiply(Normal, factor);
			D = D * factor;
		}

		/*public PlaneIntersectionType Intersects(BoundingBox box)
		{
			return box.Intersects(this);
		}

		public void Intersects(ref BoundingBox box, out PlaneIntersectionType result)
		{
			box.Intersects(ref this, out result);
		}

		public PlaneIntersectionType Intersects(BoundingSphere sphere)
		{
			return sphere.Intersects(this);
		}

		public void Intersects(ref BoundingSphere sphere, out PlaneIntersectionType result)
		{
			sphere.Intersects(ref this, out result);
		}

		public PlaneIntersectionType Intersects(BoundingFrustum frustum)
		{
			return frustum.Intersects(this);
		}*/

		#endregion

		#region Internal Methods

		internal PlaneIntersectionType Intersects(ref Vector3 point)
		{
			float distance;
			DotCoordinate(ref point, out distance);
			if (distance > 0)
			{
				return PlaneIntersectionType.Front;
			}
			if (distance < 0)
			{
				return PlaneIntersectionType.Back;
			}
			return PlaneIntersectionType.Intersecting;
		}

		#endregion

		#region Public Static Methods

		public static Plane Normalize(Plane value)
		{
			Plane ret;
			Normalize(value, out ret);
			return ret;
		}

		public static void Normalize(Plane value, out Plane result)
		{
			float length = value.Normal.Length;
			float factor = 1.0f / length;
			result.Normal = Vector3.Multiply(value.Normal, factor);
			result.D = value.D * factor;
		}

		/// <summary>
		/// Transforms a normalized plane by a matrix.
		/// </summary>
		/// <param name="plane">The normalized plane to transform.</param>
		/// <param name="matrix">The transformation matrix.</param>
		/// <returns>The transformed plane.</returns>
		public static Plane Transform(Plane plane, Matrix4 matrix)
		{
			Plane result;
			Transform(plane, matrix, out result);
			return result;
		}

		/// <summary>
		/// Transforms a normalized plane by a matrix.
		/// </summary>
		/// <param name="plane">The normalized plane to transform.</param>
		/// <param name="matrix">The transformation matrix.</param>
		/// <param name="result">The transformed plane.</param>
		public static void Transform(
			Plane plane,
			Matrix4 matrix,
			out Plane result
		) {
			/* See "Transforming Normals" in
			 * http://www.glprogramming.com/red/appendixf.html
			 * for an explanation of how this works.
			 */
			Matrix4 transformedMatrix;
			transformedMatrix = Matrix4.Invert(matrix);
			transformedMatrix = Matrix4.Transpose(transformedMatrix);
			Vector4 vector = Vector4(plane.Normal, plane.D);
			Vector4 transformedVector;
			Vector4.Transform(
				vector,
				transformedMatrix,
				out transformedVector
			);
			result = Plane(transformedVector);
		}

		/// <summary>
		/// Transforms a normalized plane by a quaternion rotation.
		/// </summary>
		/// <param name="plane">The normalized plane to transform.</param>
		/// <param name="rotation">The quaternion rotation.</param>
		/// <returns>The transformed plane.</returns>
		public static Plane Transform(Plane plane, Quaternion rotation)
		{
			Plane result;
			Transform(plane, rotation, out result);
			return result;
		}

		/// <summary>
		/// Transforms a normalized plane by a quaternion rotation.
		/// </summary>
		/// <param name="plane">The normalized plane to transform.</param>
		/// <param name="rotation">The quaternion rotation.</param>
		/// <param name="result">The transformed plane.</param>
		public static void Transform(
			Plane plane,
			Quaternion rotation,
			out Plane result
		) {
			result.Normal = Vector3.Transform(
				plane.Normal,
				rotation
			);
			result.D = plane.D;
		}

		#endregion

		#region Public Static Operators and Override Methods

		public static bool operator !=(Plane plane1, Plane plane2)
		{
			return !plane1.Equals(plane2);
		}

		public static bool operator ==(Plane plane1, Plane plane2)
		{
			return plane1.Equals(plane2);
		}

		public bool Equals(Plane other)
		{
			return (Normal == other.Normal && D == other.D);
		}

		public int GetHashCode()
		{
			return Normal.GetHashCode() ^ D.GetHashCode();
		}

		public override void ToString(String str)
		{
			str.AppendF($"{Normal:{Normal} D:{D}}");
		}

		#endregion
	}
}
