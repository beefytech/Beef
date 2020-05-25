using System;

namespace Beefy.utils
{
	static class BeefPerf
	{
		[CallingConvention(.Stdcall), CLink]
		extern static void BpInit(char8* severName, char8* sessionName);

		[CallingConvention(.Stdcall), CLink]
		extern static void BpShutdown();

		[CallingConvention(.Stdcall), CLink]
		extern static void BpRetryConnect();

		[CallingConvention(.Stdcall), CLink]
		extern static void BpPause();

		[CallingConvention(.Stdcall), CLink]
		extern static void BpUnpause();

		[CallingConvention(.Stdcall), CLink]
		extern static void BpSetThreadName(char8* threadName);

		[CallingConvention(.Stdcall), CLink]
		extern static void BpEnter(char8* name);        

		[CallingConvention(.Stdcall), CLink]
		extern static void BpLeave();

		[CallingConvention(.Stdcall), CLink]
		extern static void BpEvent(char8* name, char8* details);

		[CallingConvention(.Stdcall), CLink]
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
