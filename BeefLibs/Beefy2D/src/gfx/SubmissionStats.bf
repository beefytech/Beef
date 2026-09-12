using System;

namespace Beefy.gfx;

public static class SubmissionStats
{
	public enum Category : int32 { Excluded, Main, Shadow, Other }

	public struct Counts
	{
		public int64 mTriangles, mDraws, mInstances;

		public static Self operator+(Self a, Self b) => .() {
			mTriangles = a.mTriangles + b.mTriangles,
			mDraws = a.mDraws + b.mDraws,
			mInstances = a.mInstances + b.mInstances };
		public static Self operator-(Self a, Self b) => .() {
			mTriangles = a.mTriangles - b.mTriangles,
			mDraws = a.mDraws - b.mDraws,
			mInstances = a.mInstances - b.mInstances };
	}

	public struct CategoryScope : IDisposable
	{
		int32 mPrevious;
		public this(Category category) { mPrevious = Gfx_SubmissionStats_SetCategory((.)category); }
		public void Dispose() { Gfx_SubmissionStats_SetCategory(mPrevious); }
	}

	[CallingConvention(.Stdcall), CLink]
	static extern int32 Gfx_SubmissionStats_SetCategory(int32 category);
	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_SubmissionStats_Get(int32 category, int64* triangles, int64* draws, int64* instances);

	// Categories are captured when queued; counts advance only when draws execute.
	public static CategoryScope Push(Category category) => .(category);
	public static Counts Read(Category category)
	{
		Counts counts = default;
		Gfx_SubmissionStats_Get((.)category, &counts.mTriangles, &counts.mDraws, &counts.mInstances);
		return counts;
	}
}
