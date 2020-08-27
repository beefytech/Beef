namespace System.Numerics
{
	[UnderlyingArray(typeof(uint8), 16, true), Align(16), Union]
	struct v128
	{
		public int8[16] int8;
		public uint8[16] uint8;
		public int16[8] int16;
		public uint16[8] uint16;
		public int32[4] int32;
		public uint32[4] uint32;
		public int64[2] int64;
		public uint64[2] uint64;
		public float[4] float;
		public double[2] double;

		[Inline]
		public this(int8 v0)
		{
			this.int8 = .(v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0);
		}

		[Inline]
		public this(uint8 v0)
		{
			this.uint8 = .(v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0, v0);
		}

		[Inline]
		public this(int16 v0)
		{
			this.int16 = .(v0, v0, v0, v0, v0, v0, v0, v0);
		}

		[Inline]
		public this(uint16 v0)
		{
			this.uint16 = .(v0, v0, v0, v0, v0, v0, v0, v0);
		}

		[Inline]
		public this(int32 v0)
		{
			this.int32 = .(v0, v0, v0, v0);
		}

		[Inline]
		public this(uint32 v0)
		{
			this.uint32 = .(v0, v0, v0, v0);
		}

		[Inline]
		public this(int64 v0)
		{
			this.int64 = .(v0, v0);
		}

		[Inline]
		public this(uint64 v0)
		{
			this.uint64 = .(v0, v0);
		}

		[Inline]
		public this(float v0)
		{
			this.float = .(v0, v0, v0, v0);
		}

		[Inline]
		public this(double v0)
		{
			this.double = .(v0, v0);
		}

		[Inline]
		public this(int8 v0, int8 v1, int8 v2, int8 v3, int8 v4, int8 v5, int8 v6, int8 v7, int8 v8, int8 v9, int8 v10, int8 v11, int8 v12, int8 v13, int8 v14, int8 v15)
		{
			this.int8 = .(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
		}

		[Inline]
		public this(uint8 v0, uint8 v1, uint8 v2, uint8 v3, uint8 v4, uint8 v5, uint8 v6, uint8 v7, uint8 v8, uint8 v9, uint8 v10, uint8 v11, uint8 v12, uint8 v13, uint8 v14, uint8 v15)
		{
			this.uint8 = .(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
		}

		[Inline]
		public this(int16 v0, int16 v1, int16 v2, int16 v3, int16 v4, int16 v5, int16 v6, int16 v7)
		{
			this.int16 = .(v0, v1, v2, v3, v4, v5, v6, v7);
		}

		[Inline]
		public this(uint16 v0, uint16 v1, uint16 v2, uint16 v3, uint16 v4, uint16 v5, uint16 v6, uint16 v7)
		{
			this.uint16 = .(v0, v1, v2, v3, v4, v5, v6, v7);
		}

		[Inline]
		public this(int32 v0, int32 v1, int32 v2, int32 v3)
		{
			this.int32 = .(v0, v1, v2, v3);
		}

		[Inline]
		public this(uint32 v0, uint32 v1, uint32 v2, uint32 v3)
		{
			this.uint32 = .(v0, v1, v2, v3);
		}

		[Inline]
		public this(int64 v0, int64 v1)
		{
			this.int64 = .(v0, v1);
		}

		[Inline]
		public this(uint64 v0, uint64 v1)
		{
			this.uint64 = .(v0, v1);
		}

		[Inline]
		public this(float v0, float v1, float v2, float v3)
		{
			this.float = .(v0, v1, v2, v3);
		}

		[Inline]
		public this(double v0, double v1)
		{
			this.double = .(v0, v1);
		}

		[Intrinsic("and")]
		public static extern v128 operator&(v128 lhs, v128 rhs);
		[Intrinsic("not")]
		public static extern v128 operator~(v128 lhs);
	}
}
