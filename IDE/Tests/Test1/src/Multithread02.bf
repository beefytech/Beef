#pragma warning disable 168

using System.Threading;
using System;

namespace IDETest
{
	class Multithread02
	{
		public static WaitEvent sEvent0 = new WaitEvent() ~ delete _;
		public static WaitEvent sEvent1 = new WaitEvent() ~ delete _;
		public static int sVal0;
		public static int sVal1;

		class ClassA
		{
			public int mA;

			[AlwaysInclude]
			public int GetValWithWait()
			{
				Thread.Sleep(1000);
				return mA;
			}
		}

		public static void Thread1()
		{
			Thread.CurrentThread.SetName("Test_Thread1");

			ClassA ca = scope .();
			ca.mA = 100;
			Thread.Sleep(100);

			//Thread1_0
			sEvent0.Set();
			Interlocked.Increment(ref sVal1);
			sEvent1.WaitFor();

			//Thread1_1
			Interlocked.Increment(ref sVal1);
		}

		public static void DoTest()
		{
			ClassA ca = scope .();
			ca.mA = 9;

			Thread thread1 = scope .(new => Thread1);
			thread1.Start(false);
			Interlocked.Increment(ref sVal0);
			Thread.Sleep(500);
			Interlocked.Increment(ref sVal0);

			sEvent0.WaitFor();
			sEvent1.Set();

			thread1.Join();
		}

		public static void Test()
		{
			//Test_Start
			bool doTest = false;
			if (doTest)
			{
				DoTest();
			}
		}
	}
}
