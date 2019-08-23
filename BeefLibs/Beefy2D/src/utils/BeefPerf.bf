using System;
using System.Runtime.InteropServices;

namespace Beefy.utils
{
	static class BeefPerf
	{
		[StdCall, CLink]
		extern static void BpInit(char8* severName, char8* sessionName);

		[StdCall, CLink]
		extern static void BpShutdown();

		[StdCall, CLink]
		extern static void BpRetryConnect();

		[StdCall, CLink]
		extern static void BpPause();

		[StdCall, CLink]
		extern static void BpUnpause();

		[StdCall, CLink]
		extern static void BpSetThreadName(char8* threadName);

		[StdCall, CLink]
		extern static void BpEnter(char8* name);        

		[StdCall, CLink]
		extern static void BpLeave();

		[StdCall, CLink]
		extern static void BpEvent(char8* name, char8* details);

		[StdCall, CLink]
		extern static char8* BpDynStr(char8* string);

		public static void Init(StringView serverName, StringView sessionName)
		{
			BpInit(serverName.ToScopeCStr!(), sessionName.ToScopeCStr!());
		}

		public static void RetryConnect()
		{
			BpRetryConnect();
		}

		public static void Shutdown()
		{
			BpShutdown();
		}

		public static void Pause()
		{
			BpPause();
		}

		public static void Unpause()
		{
			BpUnpause();
		}

		public static void SetThreadName(StringView threadName)
		{
			BpSetThreadName(threadName.ToScopeCStr!());
		}

		[Inline]
		public static void Enter(char8* name)
		{
			BpEnter(name);
		}

		[Inline]
		public static void Leave()
		{
			BpLeave();
		}

		[Inline]
		public static void Event(char8* name, char8* details)
		{
			BpEvent(name, details);
		}

		[Inline]
		public static char8* DynStr(char8* string)
		{
			return BpDynStr(string);
		}
	}

	class AutoBeefPerf
	{
		public this(char8* name)
		{
			BeefPerf.Enter(name);
		}

		public ~this()
		{
			BeefPerf.Leave();
		}
	}
}
