#pragma warning disable 168

using System;
using System.Threading;
using System.Diagnostics;

namespace IDETest
{
	class HotSwap_TLS
	{
		class ClassA
		{
			[ThreadStatic]
			public static int sTLS0;

			/*ClassA_TLS1
			[ThreadStatic]
			public static int sTLS1;
			*/
		}

		static Monitor sMonitor = new .() ~ delete _;
		static WaitEvent sEvent = new .() ~ delete _;
		static WaitEvent sDoneEvent = new .() ~ delete _;
		static int sThreadResult;

		public static int Inc0()
		{
			return ++ClassA.sTLS0;
		}

		public static int Inc1()
		{
			/*Inc1_TLS1
			return ++ClassA.sTLS1;
			*/
#unwarn
			return -1;
		}

		public static void Thread0()
		{
			sEvent.WaitFor();
			sThreadResult = Inc0();
			sDoneEvent.Set();

			sEvent.WaitFor();
			sThreadResult = Inc0();
			sDoneEvent.Set();

			sEvent.WaitFor();
			sThreadResult = Inc1();
			sDoneEvent.Set();

			sEvent.WaitFor();
			sThreadResult = Inc1();
			sDoneEvent.Set();
		}

		public static void Test()
		{
			//Test_Start
			let thread = scope Thread(new => Thread0);
			thread.Start(false);

			int val = Inc0();
			val = Inc0();
			val = Inc0();
			Debug.Assert(val == 3);

			sEvent.Set();
			sDoneEvent.WaitFor();
			Debug.Assert(sThreadResult == 1);

			sEvent.Set();
			sDoneEvent.WaitFor();
			Debug.Assert(sThreadResult == 2);

			val = Inc1();
			val = Inc1();
			val = Inc1();
			NOP!();//Debug.Assert(val == 3);

			sEvent.Set();
			sDoneEvent.WaitFor();
			NOP!();//Debug.Assert(sThreadResult == 1);

			sEvent.Set();
			sDoneEvent.WaitFor();
			NOP!();//Debug.Assert(sThreadResult == 2);

			thread.Join();
		}
	}
}
