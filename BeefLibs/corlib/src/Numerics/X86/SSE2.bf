namespace System.Numerics.X86
{
	static class SSE2
	{
		public static bool IsSupported => Runtime.Features.SSE2;
	}
}
