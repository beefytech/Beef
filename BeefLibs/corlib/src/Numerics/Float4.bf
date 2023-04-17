namespace System.Numerics
{
	[UnderlyingArray(typeof(float), 4, true)]
	struct float4
	{
		public float x;
		public float y;
		public float z;
		public float w;

		[Inline]
		public this()
		{
			this = default;
		}

		[Inline]
		public this(float x, float y, float z, float w)
		{
			this.x = x;
			this.y = y;
			this.z = z;
			this.w = w;
		}

		public extern float this[int idx] { [Intrinsic("index")] get; [Intrinsic("index")] set; }

		public extern float4 wzyx { [Intrinsic("shuffle3210")] get; [Intrinsic("shuffle3210")] set; }

		[Intrinsic("min")]
		public static extern float4 min(float4 lhs, float4 rhs);

		[Intrinsic("max")]
		public static extern float4 max(float4 lhs, float4 rhs);

		[Intrinsic("add")]
		public static extern float4 operator+(float4 lhs, float4 rhs);
		[Intrinsic("add"), Commutable]
		public static extern float4 operator+(float4 lhs, float rhs);
		[Intrinsic("add")]
		public static extern float4 operator++(float4 lhs);

		[Intrinsic("sub")]
		public static extern float4 operator-(float4 lhs, float4 rhs);
		[Intrinsic("sub"), Commutable]
		public static extern float4 operator-(float4 lhs, float rhs);
		[Intrinsic("sub")]
		public static extern float4 operator--(float4 lhs);

		[Intrinsic("mul")]
		public static extern float4 operator*(float4 lhs, float4 rhs);
		[Intrinsic("mul"), Commutable]
		public static extern float4 operator*(float4 lhs, float rhs);

		[Intrinsic("div")]
		public static extern float4 operator/(float4 lhs, float4 rhs);
		[Intrinsic("div")]
		public static extern float4 operator/(float4 lhs, float rhs);
		[Intrinsic("div")]
		public static extern float4 operator/(float lhs, float4 rhs);

		[Intrinsic("mod")]
		public static extern float4 operator%(float4 lhs, float4 rhs);
		[Intrinsic("mod")]
		public static extern float4 operator%(float4 lhs, float rhs);
		[Intrinsic("mod")]
		public static extern float4 operator%(float lhs, float4 rhs);

		[Intrinsic("eq")]
		public static extern bool4 operator==(float4 lhs, float4 rhs);
		[Intrinsic("eq"), Commutable]
		public static extern bool4 operator==(float4 lhs, float rhs);

		[Intrinsic("neq")]
		public static extern bool4 operator!=(float4 lhs, float4 rhs);
		[Intrinsic("neq"), Commutable]
		public static extern bool4 operator!=(float4 lhs, float rhs);
		
		[Intrinsic("lt")]
		public static extern bool4 operator<(float4 lhs, float4 rhs);
		[Intrinsic("lt")]
		public static extern bool4 operator<(float4 lhs, float rhs);

		[Intrinsic("lte")]
		public static extern bool4 operator<=(float4 lhs, float4 rhs);
		[Intrinsic("lte")]
		public static extern bool4 operator<=(float4 lhs, float rhs);

		[Intrinsic("gt")]
		public static extern bool4 operator>(float4 lhs, float4 rhs);
		[Intrinsic("gt")]
		public static extern bool4 operator>(float4 lhs, float rhs);

		[Intrinsic("gte")]
		public static extern bool4 operator>=(float4 lhs, float4 rhs);
		[Intrinsic("gte")]
		public static extern bool4 operator>=(float4 lhs, float rhs);

		[Intrinsic("cast")]
		public static extern explicit operator v128(float4 lhs);
		[Intrinsic("cast")]
		public static extern explicit operator float4(v128 lhs);
	}
}
