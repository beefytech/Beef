using System;
using System.Threading;

namespace AttachTest
{
	class Program
	{
#if BF_PLATFORM_LINUX
		[CLink] static extern int32 prctl(int32 option, uint arg2, uint arg3, uint arg4, uint arg5);
#endif

		static int sCounter;

		static void Main()
		{
#if BF_PLATFORM_LINUX
			// PR_SET_PTRACER, PR_SET_PTRACER_ANY: let a non-parent debugger attach under Yama
			prctl(0x59616d61, (uint)-1, 0, 0, 0);
#endif
			Console.WriteLine("ready");
			while (sCounter < 100000)
			{
				sCounter++;
				//AttachTest_Loop
				Thread.Sleep(10);
			}
		}
	}
}
