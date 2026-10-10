#pragma warning disable 168

using System;
using System.Threading;

namespace IDETest
{
	class ThreadFreeze
	{
		static int sCountA;
		static int sCountB;
		static bool sDone;
		static WaitEvent sStartedA = new .() ~ delete _;
		static WaitEvent sStartedB = new .() ~ delete _;

		static void WorkerA()
		{
			sStartedA.Set();
			while (!sDone)
			{
				sCountA++;
				Thread.Sleep(1);
			}
		}

		static void WorkerB()
		{
			sStartedB.Set();
			while (!sDone)
			{
				sCountB++;
				Thread.Sleep(1);
			}
		}

		public static void Test()
		{
			//ThreadFreeze_Test
			bool doTest = false;
			if (!doTest)
				return;

			Thread.CurrentThread.SetName("MainThread");
			Thread threadA = scope .(new => WorkerA);
			threadA.SetName("WorkerA");
			threadA.Start(false);
			Thread threadB = scope .(new => WorkerB);
			threadB.SetName("WorkerB");
			threadB.Start(false);
			sStartedA.WaitFor();
			sStartedB.WaitFor();

			int prevA = sCountA;
			int prevB = sCountB;
			int deltaA = 0;
			int deltaB = 0;
			bool stop = false;
			for (int i < 200)
			{
				Thread.Sleep(20);
				deltaA = sCountA - prevA;
				deltaB = sCountB - prevB;
				prevA = sCountA;
				prevB = sCountB;
				//ThreadFreeze_Loop
				if (stop)
					break;
			}

			sDone = true;
			threadA.Join();
			threadB.Join();
		}
	}
}
