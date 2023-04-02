namespace System.Numerics.X86
{
	static class SSE
	{
		public static bool IsSupported => Runtime.Features.SSE;

		[Inline]
		public static v128 add_ps(v128 a, v128 b) => (.) ((float4) a + (float4) b);
		[Inline]
		public static v128 sub_ps(v128 a, v128 b) => (.) ((float4) a - (float4) b);
		[Inline]
		public static v128 mul_ps(v128 a, v128 b) => (.) ((float4) a * (float4) b);
		[Inline]
		public static v128 div_ps(v128 a, v128 b) => (.) ((float4) a / (float4) b);

		[Inline]
		public static v128 min_ps(v128 a, v128 b) => (.) float4.min((.) a, (.) b);
		[Inline]
		public static v128 max_ps(v128 a, v128 b) => (.) float4.max((.) a, (.) b);

		[Inline]
		public static v128 add_ss(v128 a, v128 b)
		{
			var res = a;
			res.float[0] += b.float[0];
			return res;
		}

		[Inline]
		public static v128 andnot_ps(v128 a, v128 b)
		{
			return ~a & b;
		}	
		
		public static extern v128 and_ps(v128 a, v128 b);
		
		public static extern v128 cmpeq_ps(v128 a, v128 b);
		
		public static extern v128 cmpeq_ss(v128 a, v128 b);
		
		public static extern v128 cmpge_ps(v128 a, v128 b);
		
		public static extern v128 cmpge_ss(v128 a, v128 b);
		
		public static extern v128 cmpgt_ps(v128 a, v128 b);
		
		public static extern v128 cmpgt_ss(v128 a, v128 b);
		
		public static extern v128 cmple_ps(v128 a, v128 b);
		
		public static extern v128 cmple_ss(v128 a, v128 b);
		
		public static extern v128 cmplt_ps(v128 a, v128 b);
		
		public static extern v128 cmplt_ss(v128 a, v128 b);
		
		public static extern v128 cmpneq_ps(v128 a, v128 b);
		
		public static extern v128 cmpneq_ss(v128 a, v128 b);
		
		public static extern v128 cmpnge_ps(v128 a, v128 b);
		
		public static extern v128 cmpnge_ss(v128 a, v128 b);
		
		public static extern v128 cmpngt_ps(v128 a, v128 b);
		
		public static extern v128 cmpngt_ss(v128 a, v128 b);
		
		public static extern v128 cmpnle_ps(v128 a, v128 b);
		
		public static extern v128 cmpnle_ss(v128 a, v128 b);
		
		public static extern v128 cmpnlt_ps(v128 a, v128 b);
		
		public static extern v128 cmpnlt_ss(v128 a, v128 b);
		
		public static extern v128 cmpord_ps(v128 a, v128 b);
		
		public static extern v128 cmpord_ss(v128 a, v128 b);
		
		public static extern v128 cmpunord_ps(v128 a, v128 b);
		
		public static extern v128 cmpunord_ss(v128 a, v128 b);
		
		public static extern int32 comieq_ss(v128 a, v128 b);
		
		public static extern int32 comige_ss(v128 a, v128 b);
		
		public static extern int32 comigt_ss(v128 a, v128 b);
		
		public static extern int32 comile_ss(v128 a, v128 b);
		
		public static extern int32 comilt_ss(v128 a, v128 b);
		
		public static extern int32 comineq_ss(v128 a, v128 b);
		
		public static extern v128 cvtsi32_ss(v128 a, int32 b);
		
		public static extern v128 cvtsi64_ss(v128 a, int64 b);
		
		public static extern float cvtss_f32(v128 a);
		
		public static extern int32 cvtss_si32(v128 a);
		
		public static extern int64 cvtss_si64(v128 a);
		
		public static extern int32 cvttss_si32(v128 a);
		
		public static extern int64 cvttss_si64(v128 a);
		
		public static extern int32 cvtt_ss2si(v128 a);
		
		public static extern int32 cvt_ss2si(v128 a);
		
		public static extern v128 div_ss(v128 a, v128 b);
		
		public static extern v128 loadu_ps(void* ptr);
		
		public static extern v128 loadu_si16(void* mem_addr);
		
		public static extern v128 loadu_si64(void* mem_addr);
		
		public static extern v128 load_ps(void* ptr);
		
		public static extern v128 max_ss(v128 a, v128 b);
		
		public static extern v128 min_ss(v128 a, v128 b);
		
		public static extern v128 movehl_ps(v128 a, v128 b);
		
		public static extern v128 movelh_ps(v128 a, v128 b);
		
		public static extern int32 movemask_ps(v128 a);
		
		public static extern v128 move_ss(v128 a, v128 b);
		
		public static extern v128 mul_ss(v128 a, v128 b);
		
		public static extern v128 or_ps(v128 a, v128 b);
		
		public static extern v128 rcp_ps(v128 a);
		
		public static extern v128 rcp_ss(v128 a);
		
		public static extern v128 rsqrt_ps(v128 a);
		
		public static extern v128 rsqrt_ss(v128 a);
		
		public static extern v128 set1_ps(float a);
		
		public static extern v128 setr_ps(float e3, float e2, float e1, float e0);
		
		public static extern v128 setzero_ps();
		
		public static extern v128 set_ps(float e3, float e2, float e1, float e0);
		
		public static extern v128 set_ps1(float a);
		
		public static extern v128 set_ss(float a);
		public static extern int32 SHUFFLE(int32 d, int32 c, int32 b, int32 a);
		
		public static extern v128 shuffle_ps(v128 a, v128 b, int32 imm8);
		
		public static extern v128 sqrt_ps(v128 a);
		
		public static extern v128 sqrt_ss(v128 a);
		
		public static extern void storeu_ps(void* ptr, v128 val);
		public static extern void storeu_si16(void* mem_addr, v128 a);
		
		public static extern void storeu_si64(void* mem_addr, v128 a);
		
		public static extern void store_ps(void* ptr, v128 val);
		
		public static extern void stream_ps(void* mem_addr, v128 a);
		
		public static extern v128 sub_ss(v128 a, v128 b);
		
		public static extern void TRANSPOSE4_PS(ref v128 row0, ref v128 row1, ref v128 row2, ref v128 row3);
		
		public static extern int32 ucomieq_ss(v128 a, v128 b);
		
		public static extern int32 ucomige_ss(v128 a, v128 b);
		
		public static extern int32 ucomigt_ss(v128 a, v128 b);
		
		public static extern int32 ucomile_ss(v128 a, v128 b);
		
		public static extern int32 ucomilt_ss(v128 a, v128 b);
		
		public static extern int32 ucomineq_ss(v128 a, v128 b);
		
		public static extern v128 unpackhi_ps(v128 a, v128 b);
		
		public static extern v128 unpacklo_ps(v128 a, v128 b);

		//[Intrinsic("x86:x86_sse_cmp_ss")]
		public static extern v128 xor_ps(v128 a, v128 b);
	}
}
