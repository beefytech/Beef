using System.Threading;

namespace System.Diagnostics
{
	enum ProfilerScope
	{
		Thread,
		Process
	}

	struct ProfileInstance : int32
	{
		public void Dispose()
		{
			String str = scope String();
			str.Append("StopSampling\t");
			((int32)this).ToString(str);
			Internal.ProfilerCmd(str);
		}
	}

	class Profiler
	{
		public enum Priority
		{
			Low,
			Normal,
			High
		}

		static int32 gProfileId = 1;

		public struct AutoLeave
		{
			//Profiler mProfiler;

			public this(/*Profiler profiler*/)
			{
				//mProfiler = profiler;
			}

			void Dispose()
			{
				Profiler.LeaveSection();
			}
		}

		static Result<ProfileInstance> StartSampling(int threadId, StringView profileDesc, int sampleRate)
		{
			int32 curId = Interlocked.Increment(ref gProfileId);

			String str = scope String();
			str.Append("StartSampling\t");
			curId.ToString(str);			
			str.Append("\t");
			threadId.ToString(str);
			str.Append("\t");
			sampleRate.ToString(str);
			str.Append("\t");
			str.Append(profileDesc);
			Internal.ProfilerCmd(str);
			return (ProfileInstance)curId;
		}

		public static Result<ProfileInstance> StartSampling(Thread thread, StringView profileDesc = default, int sampleRate = -1)
		{
			return StartSampling(thread.Id, profileDesc, sampleRate);
		}

		public static Result<ProfileInstance> StartSampling(StringView profileDesc = default, int sampleRate = -1)
		{
			return StartSampling(0, profileDesc, sampleRate);
		}

		public static void ClearSampling()
		{
			Internal.ProfilerCmd("ClearSampling");
		}

		public void Mark()
		{

		}

		public static AutoLeave EnterSection(StringView name, Priority priority = Priority.Normal)
		{
			return AutoLeave();
		}

		public static void LeaveSection()
		{

		}
	}
}
