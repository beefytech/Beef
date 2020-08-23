namespace System.Numerics
{
	[UnderlyingArray(typeof(bool), 4, true)]
	struct bool4
	{
		public bool x;
		public bool y;
		public bool z;
		public bool w;

		[Intrinsic("and")]
		public static extern bool4 operator&(bool4 lhs, bool4 rhs);
	}
}
