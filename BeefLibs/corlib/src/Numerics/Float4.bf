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

		public extern float4 WZYX { [Intrinsic("shuffle3210")] get; [Intrinsic("shuffle3210")] set; }

		// Legacy
		[NoShow]
		public extern float4 wzyx { [Intrinsic("shuffle3210")] get; [Intrinsic("shuffle3210")] set; }
		[NoShow, Inline]
		public static float4 min(float4 lhs, float4 rhs) => Min(lhs, rhs);
		[NoShow, Inline]
		public static float4 max(float4 lhs, float4 rhs) => Max(lhs, rhs);

		// As with MINPS/MAXPS, equal or unordered lanes select rhs.
		[Inline]
		public static float4 Min(float4 lhs, float4 rhs) => Select(lhs < rhs, lhs, rhs);

		[Inline]
		public static float4 Max(float4 lhs, float4 rhs) => Select(lhs > rhs, lhs, rhs);

		[Intrinsic("vector_abs")]
		public static extern float4 Abs(float4 value);

		[Intrinsic("vector_sqrt")]
		public static extern float4 Sqrt(float4 value);

		/// Reciprocal of sqrt, with the rounding of those two operations.
		[Inline]
		public static float4 RSqrt(float4 value) => 1.0f / Sqrt(value);

		/// Approximate reciprocal square root. Accuracy and exceptional-value behavior
		/// are target dependent; subnormals may be treated as zero. A backend may use
		/// the more accurate 1/sqrt fallback. Does not require fast-math mode.
		[Intrinsic("vector_rsqrt_estimate")]
		public static extern float4 RSqrtEstimate(float4 value);

		/// Fused multiply-add, with one rounding per lane, even without hardware FMA.
		[Intrinsic("vector_fma")]
		public static extern float4 FusedMultiplyAdd(float4 x, float4 y, float4 z);

		/// Select trueValue where the corresponding mask lane is true.
		[Intrinsic("vector_select")]
		public static extern float4 Select(bool4 mask, float4 trueValue, float4 falseValue);

		/// Indices must be compile-time constants in 0..3.
		[Intrinsic("shuffle")]
		public static extern float4 ShuffleVector(float4 value, int x, int y, int z, int w);

		/// Constant indices 0..3 select from a, and 4..7 select from b.
		[Intrinsic("shuffle")]
		public static extern float4 ShuffleVector(float4 a, float4 b, int x, int y, int z, int w);

		/// Read/write four floats. Only normal float alignment (4 bytes) is required.
		[Inline]
		public static float4 Load(float* source) => *(float4*)source;
		[Inline]
		public void Store(float* destination) => *(float4*)destination = this;

		/// The caller must supply a 16-byte-aligned address with four accessible floats.
		[Inline]
		public static float4 LoadAligned(float* source) => (float4)*(v128*)source;
		[Inline]
		public void StoreAligned(float* destination) => *(v128*)destination = (v128)this;

		[Intrinsic("add")]
		public static extern float4 operator+(float4 lhs, float4 rhs);
		[Intrinsic("add"), Commutable]
		public static extern float4 operator+(float4 lhs, float rhs);
		[Intrinsic("add")]
		public static extern float4 operator++(float4 lhs);

		[Intrinsic("sub")]
		public static extern float4 operator-(float4 lhs, float4 rhs);
		[Intrinsic("sub")]
		public static extern float4 operator-(float4 lhs, float rhs);
		[Intrinsic("sub")]
		public static extern float4 operator-(float lhs, float4 rhs);
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
