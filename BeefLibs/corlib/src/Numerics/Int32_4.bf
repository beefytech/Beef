namespace System.Numerics
{
	[UnderlyingArray(typeof(int32), 4, true)]
	struct int32_4
	{
		public int32 x;
		public int32 y;
		public int32 z;
		public int32 w;

		[Inline]
		public this()
		{
			this = default;
		}

		[Inline]
		public this(int32 x, int32 y, int32 z, int32 w)
		{
			this.x = x;
			this.y = y;
			this.z = z;
			this.w = w;
		}

		public extern int32 this[int32 idx] { [Intrinsic("index")] get; [Intrinsic("index")] set; }

		public extern int32_4 wzyx { [Intrinsic("shuffle3210")] get; [Intrinsic("shuffle3210")] set; }

		[Intrinsic("not")]
		public static extern int32_4 operator~(int32_4 lhs);

		[Intrinsic("add")]
		public static extern int32_4 operator+(int32_4 lhs, int32_4 rhs);
		[Intrinsic("add"), Commutable]
		public static extern int32_4 operator+(int32_4 lhs, int32 rhs);
		[Intrinsic("add")]
		public static extern int32_4 operator++(int32_4 lhs);

		[Intrinsic("sub")]
		public static extern int32_4 operator-(int32_4 lhs, int32_4 rhs);
		[Intrinsic("sub"), Commutable]
		public static extern int32_4 operator-(int32_4 lhs, int32 rhs);
		[Intrinsic("sub")]
		public static extern int32_4 operator--(int32_4 lhs);

		[Intrinsic("mul")]
		public static extern int32_4 operator*(int32_4 lhs, int32_4 rhs);
		[Intrinsic("mul"), Commutable]
		public static extern int32_4 operator*(int32_4 lhs, int32 rhs);

		[Intrinsic("div")]
		public static extern int32_4 operator/(int32_4 lhs, int32_4 rhs);
		[Intrinsic("div")]
		public static extern int32_4 operator/(int32_4 lhs, int32 rhs);
		[Intrinsic("div")]
		public static extern int32_4 operator/(int32 lhs, int32_4 rhs);

		[Intrinsic("mod")]
		public static extern int32_4 operator%(int32_4 lhs, int32_4 rhs);
		[Intrinsic("mod")]
		public static extern int32_4 operator%(int32_4 lhs, int32 rhs);
		[Intrinsic("mod")]
		public static extern int32_4 operator%(int32 lhs, int32_4 rhs);

		[Intrinsic("and")]
		public static extern int32_4 operator&(int32_4 lhs, int32_4 rhs);
		[Intrinsic("and")]
		public static extern int32_4 operator&(int32_4 lhs, int32 rhs);
		[Intrinsic("and")]
		public static extern int32_4 operator&(int32 lhs, int32_4 rhs);

		[Intrinsic("or")]
		public static extern int32_4 operator|(int32_4 lhs, int32_4 rhs);
		[Intrinsic("or")]
		public static extern int32_4 operator|(int32_4 lhs, int32 rhs);
		[Intrinsic("or")]
		public static extern int32_4 operator|(int32 lhs, int32_4 rhs);

		[Intrinsic("xor")]
		public static extern int32_4 operator^(int32_4 lhs, int32_4 rhs);
		[Intrinsic("xor")]
		public static extern int32_4 operator^(int32_4 lhs, int32 rhs);
		[Intrinsic("xor")]
		public static extern int32_4 operator^(int32 lhs, int32_4 rhs);

		[Intrinsic("shl")]
		public static extern int32_4 operator<<(int32_4 lhs, int rhs);

		[Intrinsic("sar")]
		public static extern int32_4 operator>>(int32_4 lhs, int rhs);

		[Intrinsic("eq")]
		public static extern bool4 operator==(int32_4 lhs, int32_4 rhs);
		[Intrinsic("eq"), Commutable]
		public static extern bool4 operator==(int32_4 lhs, int32 rhs);

		[Intrinsic("neq")]
		public static extern bool4 operator!=(int32_4 lhs, int32_4 rhs);
		[Intrinsic("neq"), Commutable]
		public static extern bool4 operator!=(int32_4 lhs, int32 rhs);
		
		[Intrinsic("lt")]
		public static extern bool4 operator<(int32_4 lhs, int32_4 rhs);
		[Intrinsic("lt")]
		public static extern bool4 operator<(int32_4 lhs, int32 rhs);

		[Intrinsic("lte")]
		public static extern bool4 operator<=(int32_4 lhs, int32_4 rhs);
		[Intrinsic("lte")]
		public static extern bool4 operator<=(int32_4 lhs, int32 rhs);

		[Intrinsic("gt")]
		public static extern bool4 operator>(int32_4 lhs, int32_4 rhs);
		[Intrinsic("gt")]
		public static extern bool4 operator>(int32_4 lhs, int32 rhs);

		[Intrinsic("gte")]
		public static extern bool4 operator>=(int32_4 lhs, int32_4 rhs);
		[Intrinsic("gte")]
		public static extern bool4 operator>=(int32_4 lhs, int32 rhs);

		[Intrinsic("cast")]
		public static extern explicit operator v128(int32_4 lhs);
		[Intrinsic("cast")]
		public static extern explicit operator int32_4(v128 lhs);
	}
}
