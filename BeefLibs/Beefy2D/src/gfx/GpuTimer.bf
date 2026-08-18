using System;

namespace Beefy.gfx;

// GPU timing for a CPU-side profiler. Spans are opened by the renderer itself around real
// submissions (draw layer flushes, MSAA resolves) -- queued draws don't execute where they were
// recorded, so bracketing caller code would measure the wrong thing. Each span carries whatever
// tag was last set, which is how the profiler attributes it to one of its sections.
//
// Results lag by a few frames (Fetch never waits on the GPU), and frames are dropped rather than
// stalling if the GPU falls behind.
static class GpuTimer
{
	[CRepr]
	public struct Span
	{
		public int32 mTag;
		public int64 mNanos;
	}

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_GpuTimer_SetEnabled(int32 enabled);

	[CallingConvention(.Stdcall), CLink]
	static extern int32 Gfx_GpuTimer_BeginFrame(int64 frameId);

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_GpuTimer_SetTag(int32 tag);

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_GpuTimer_EndFrame();

	[CallingConvention(.Stdcall), CLink]
	static extern int32 Gfx_GpuTimer_Fetch(int64* outFrameId, Span* outSpans, int32 maxSpans);

	public static void SetEnabled(bool enabled)
	{
		Gfx_GpuTimer_SetEnabled(enabled ? 1 : 0);
	}

	// False = not timing this frame (the result ring is still full).
	public static bool BeginFrame(int64 frameId)
	{
		return Gfx_GpuTimer_BeginFrame(frameId) != 0;
	}

	public static void SetTag(int32 tag)
	{
		Gfx_GpuTimer_SetTag(tag);
	}

	public static void EndFrame()
	{
		Gfx_GpuTimer_EndFrame();
	}

	// -1 = the oldest timed frame isn't finished yet; otherwise the number of spans written for
	// frameId (0 when that frame's timings had to be discarded).
	public static int32 Fetch(out int64 frameId, Span* outSpans, int32 maxSpans)
	{
		frameId = 0;
		return Gfx_GpuTimer_Fetch(&frameId, outSpans, maxSpans);
	}
}
