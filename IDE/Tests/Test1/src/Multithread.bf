#pragma warning disable 168

using System.Threading;
using System;

namespace IDETest
{
	class Multithread
	{
		public bool mDone;
		public WaitEvent mStartedProc = new WaitEvent() ~ delete _;

		//TODO: Doesn't work with anything other than 0
		[ThreadStatic]
		public static int sLocalVal = 0;

		public void DoRecurse(int depth, ref int val)
		{
			Thread.Sleep(1);
			++val;
			if (val < 5)
			{
				DoRecurse(depth + 1, ref val);
			}
		}

		public void ThreadFunc(Object obj)
		{
			int threadNum = (int)obj;
			sLocalVal++;

			String threadName = scope .();
			Thread.CurrentThread.GetName(threadName);

			mStartedProc.Set();

			//ThreadFunc
			int val = 0;
			for (int i < 200)
			{
				val = 0; DoRecurse(0, ref val);
				Thread.Sleep(1);
			}
		}

		public static void Test()
		{
			//MultithreadTester_Test
			bool doTest = false;
			if (!doTest)
			{
				return;
			}

			Multithread mt = scope Multithread();

			Thread threadA = scope .(new => mt.ThreadFunc);
			threadA.SetName("ThreadA");
			threadA.Start(0, false);
			mt.mStartedProc.WaitFor();

			Thread threadB = scope .(new => mt.ThreadFunc);
			threadB.SetName("ThreadB");
			threadB.Start(1, false);
			mt.mStartedProc.WaitFor();

			Thread threadC = scope .(new => mt.ThreadFunc);
			threadC.SetName("ThreadC");
			threadC.Start(2, false);
			mt.mStartedProc.WaitFor();

			threadA.Join();
			threadB.Join();
			threadC.Join();

			int a = 99;
		}
	}
}
