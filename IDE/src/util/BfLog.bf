using System;

namespace IDE.util
{
	class BfLog
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void BfLog_Log(char8* str);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfLog_LogDbg(char8* str);

		public static void Log(StringView str, params Object[] strParams)
		{
			var fStr = scope String();
			fStr.AppendF(str, params strParams);
			BfLog_Log(fStr);
		}

		public static void LogDbg(StringView str, params Object[] strParams)
		{
			var fStr = scope String();
			fStr.AppendF(str, params strParams);
			BfLog_LogDbg(fStr);
		}
	}
}
