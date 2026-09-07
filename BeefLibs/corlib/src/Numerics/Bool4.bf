namespace System.Numerics
{
	[UnderlyingArray(typeof(bool), 4, true)]
	struct bool4
	{
		public bool x;
		public bool y;
		public bool z;
		public bool w;

		[Inline]
		public this()
		{
			this = default;
		}

		[Inline]
		public this(bool x, bool y, bool z, bool w)
		{
			this.x = x;
			this.y = y;
			this.z = z;
			this.w = w;
		}

		[Inline]
		public static bool Any(bool4 value) => value.x | value.y | value.z | value.w;
		[Inline]
		public static bool All(bool4 value) => value.x & value.y & value.z & value.w;

		/// Bit 0 is x, bit 1 is y, bit 2 is z, and bit 3 is w.
		[Inline]
		public static int MoveMask(bool4 value) =>
			(value.x ? 1 : 0) | (value.y ? 2 : 0) | (value.z ? 4 : 0) | (value.w ? 8 : 0);

		[Inline]
		public static bool4 operator&(bool4 lhs, bool4 rhs) =>
			.(lhs.x & rhs.x, lhs.y & rhs.y, lhs.z & rhs.z, lhs.w & rhs.w);
		[Inline]
		public static bool4 operator|(bool4 lhs, bool4 rhs) =>
			.(lhs.x | rhs.x, lhs.y | rhs.y, lhs.z | rhs.z, lhs.w | rhs.w);
		[Inline]
		public static bool4 operator^(bool4 lhs, bool4 rhs) =>
			.(lhs.x ^ rhs.x, lhs.y ^ rhs.y, lhs.z ^ rhs.z, lhs.w ^ rhs.w);
		[Inline]
		public static bool4 operator!(bool4 value) => .(!value.x, !value.y, !value.z, !value.w);
	}
}
