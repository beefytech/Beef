using System;
using IDE;

namespace Debugger
{
	static class Callbacks
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void Debugger_SetCallbacks(void* callback);

		public static void Init()
		{
			Debugger_SetCallbacks(
                ((Delegate_MapLine)scope => MapHotLine).GetFuncPtr()
                );
		}

		delegate void Delegate_MapLine(char8* fileName, int32 line, int32* outHotLines, int32 hotLinesCount);
		static void MapHotLine(char8* fileName, int32 line, int32* outHotLines, int32 hotLinesCount)
		{

		}
	}
}
