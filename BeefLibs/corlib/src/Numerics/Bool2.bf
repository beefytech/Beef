namespace System.Numerics
{
	[UnderlyingArray(typeof(bool), 2, true)]
	struct bool2
	{
		public bool x;
		public bool y;
		
		[Intrinsic("and")]
		public static extern bool2 operator&(bool2 lhs, bool2 rhs);
	}
}
