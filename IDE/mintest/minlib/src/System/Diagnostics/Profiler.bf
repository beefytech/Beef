using System.Threading;

namespace System.Diagnostics
{
	enum ProfilerScope
	{
		Thread,
		Process
	}

	struct ProfileId : int32
	{
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

		static Result<ProfileId> StartSampling(int32 threadId, StringView profileDesc)
		{
			//int32 curId = Interlocked.Increment(ref gProfileId);
			int32 curId = gProfileId++;

			String str = scope String();
			str.Append("StartSampling\t");
			curId.ToString(str);			
			str.Append("\t");
			threadId.ToString(str);
			str.Append("\t");
			str.Append(profileDesc);
			Internal.ProfilerCmd(str);
			return (ProfileId)curId;
		}

		public static void StopSampling(ProfileId profileId)
		{
			String str = scope String();
			str.Append("StopSampling\t");
			((int32)profileId).ToString(str);
			Internal.ProfilerCmd(str);
		}

		public static Result<ProfileId> StartSampling(Thread thread, StringView profileDesc = default)
		{
			return StartSampling(thread.Id, profileDesc);
		}

		public static Result<ProfileId> StartSampling(StringView profileDesc = default)
		{
			return StartSampling(0, profileDesc);
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
