namespace System.Numerics
{
	[UnderlyingArray(typeof(float), 2, true)]
	struct float2
	{
		public float x;
		public float y;

		[Inline]
		public this()
		{
			this = default;
		}

		[Inline]
		public this(float x, float y)
		{
			this.x = x;
			this.y = y;
		}

		public extern float this[int idx] { [Intrinsic("index")] get; [Intrinsic("index")] set; }

		public extern float2 yx { [Intrinsic("shuffle10")] get; [Intrinsic("shuffle10")] set; }

		[Intrinsic("add")]
		public static extern float2 operator+(float2 lhs, float2 rhs);
		[Intrinsic("add"), Commutable]
		public static extern float2 operator+(float2 lhs, float rhs);
		[Intrinsic("add")]
		public static extern float2 operator++(float2 lhs);

		[Intrinsic("sub")]
		public static extern float2 operator-(float2 lhs, float2 rhs);
		[Intrinsic("sub"), Commutable]
		public static extern float2 operator-(float2 lhs, float rhs);
		[Intrinsic("sub")]
		public static extern float2 operator--(float2 lhs);

		[Intrinsic("mul")]
		public static extern float2 operator*(float2 lhs, float2 rhs);
		[Intrinsic("mul"), Commutable]
		public static extern float2 operator*(float2 lhs, float rhs);

		[Intrinsic("div")]
		public static extern float2 operator/(float2 lhs, float2 rhs);
		[Intrinsic("div")]
		public static extern float2 operator/(float2 lhs, float rhs);
		[Intrinsic("div")]
		public static extern float2 operator/(float lhs, float2 rhs);

		[Intrinsic("mod")]
		public static extern float2 operator%(float2 lhs, float2 rhs);
		[Intrinsic("mod")]
		public static extern float2 operator%(float2 lhs, float rhs);
		[Intrinsic("mod")]
		public static extern float2 operator%(float lhs, float2 rhs);

		[Intrinsic("eq")]
		public static extern bool2 operator==(float2 lhs, float2 rhs);
		[Intrinsic("eq"), Commutable]
		public static extern bool2 operator==(float2 lhs, float rhs);

		[Intrinsic("neq")]
		public static extern bool2 operator!=(float2 lhs, float2 rhs);
		[Intrinsic("neq"), Commutable]
		public static extern bool2 operator!=(float2 lhs, float rhs);
		
		[Intrinsic("lt")]
		public static extern bool2 operator<(float2 lhs, float2 rhs);
		[Intrinsic("lt")]
		public static extern bool2 operator<(float2 lhs, float rhs);

		[Intrinsic("lte")]
		public static extern bool2 operator<=(float2 lhs, float2 rhs);
		[Intrinsic("lte")]
		public static extern bool2 operator<=(float2 lhs, float rhs);

		[Intrinsic("gt")]
		public static extern bool2 operator>(float2 lhs, float2 rhs);
		[Intrinsic("gt")]
		public static extern bool2 operator>(float2 lhs, float rhs);

		[Intrinsic("gte")]
		public static extern bool2 operator>=(float2 lhs, float2 rhs);
		[Intrinsic("gte")]
		public static extern bool2 operator>=(float2 lhs, float rhs);
	}
}
