using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Diagnostics;
using Beefy.utils;

namespace IDE.Debugger
{
	class DbgProfiler
	{
		public int mIdx;
		public bool mIsManual;

		[StdCall, CLink]
		static extern void* Profiler_Delete(void* nativeProfiler);

		[StdCall, CLink]
		static extern void Profiler_Stop(void* nativeProfiler);

		[StdCall, CLink]
		static extern void Profiler_Clear(void* nativeProfiler);

		[StdCall, CLink]
		static extern char8* Profiler_GetThreadList(void* nativeProfiler);

		[StdCall, CLink]
		static extern char8* Profiler_GetOverview(void* nativeProfiler);

		[StdCall, CLink]
		static extern bool Profiler_IsRunning(void* nativeProfiler);

		[StdCall, CLink]
		static extern char8* Profiler_GetCallTree(void* nativeProfiler, int32 threadId, bool reverse);

		void* mNativeProfiler;

		public bool IsSampling
		{
			get
			{
				return Profiler_IsRunning(mNativeProfiler);
			}
		}

		public this(void* nativeProfiler)
		{
			mNativeProfiler = nativeProfiler;
		}

		public ~this()
		{
			Profiler_Delete(mNativeProfiler);
		}

		public void Stop()
		{
			Profiler_Stop(mNativeProfiler);			
		}

		public void Clear()
		{
			Profiler_Clear(mNativeProfiler);
		}

		public class Overview
		{
			public int32 mRecordedTicks;
			public int32 mEndedTicks = -1;
			public String mTitle = new String() ~ delete _;
			public String mDescription = new String() ~ delete _;
			public int32 mSamplesPerSecond;
			public int32 mTotalVirtualSamples;
			public int32 mTotalActualSamples;

			public bool IsSampling
			{
				get
				{
					return mEndedTicks == -1;
				}
			}
		}

		public void GetOverview(Overview overview)
		{
			char8* resultCStr = Profiler_GetOverview(mNativeProfiler);
			String str = scope String();
			str.Append(resultCStr);

			var lineItr = str.Split('\n');

			var dataItr = lineItr.GetNext().Get().Split('\t');
			overview.mRecordedTicks = int32.Parse(dataItr.GetNext());
			if (dataItr.GetNext() case .Ok(let sv))
				overview.mEndedTicks = int32.Parse(sv);

			dataItr = lineItr.GetNext().Get().Split('\t');
			overview.mSamplesPerSecond = int32.Parse(dataItr.GetNext());
			overview.mTotalVirtualSamples = int32.Parse(dataItr.GetNext());
			overview.mTotalActualSamples = int32.Parse(dataItr.GetNext());

			overview.mDescription.Append(str, lineItr.MatchPos + 1);
			overview.mTitle.Append(lineItr.GetNext().Get());
		}

		public void GetThreadList(String result)
		{
			char8* resultCStr = Profiler_GetThreadList(mNativeProfiler);
			result.Append(resultCStr);
		}

		public void GetCallTree(int32 threadId, String result, bool reverse)
		{
			char8* resultCStr = Profiler_GetCallTree(mNativeProfiler, threadId, reverse);
			result.Append(resultCStr);
		}

		public void GetLabel(String label)
		{
			label.AppendF("{0}", mIdx);

			var overview = scope DbgProfiler.Overview();
			GetOverview(overview);
			if (!overview.mTitle.IsEmpty)
				label.AppendF(" - {0}", overview.mTitle);
			else if (mIsManual)
				label.AppendF(" - Manual");
			else
				label.AppendF(" - Program");
		}
	}
}
