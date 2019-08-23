#pragma warning disable 168

using System;

namespace IDETest
{
	class SlotTest001
	{
		interface ITestA
		{
			int MethodA();
		}

		interface ITestB
		{
			int MethodB();
		}

		interface ITestC
		{
			int MethodC();
		}

		interface ITestD
		{
			int MethodD();
		}

		class ClassA
/*ClassA_IFace0
		: ITestA, ITestB, ITestC, ITestD
*/
		//: ITestA, ITestB, ITestC, ITestD
		{
			public virtual int VirtA()
			{
				return 931;
			}

			public int MethodA()
			{
				return 1;
			}

			public int MethodB()
			{
				return 2;
			}

			public int MethodC()
			{
				return 3;
			}

			public int MethodD()
			{
				return 4;
			}
		}

		public static int TestA()
		{
			ClassA ca = scope .();
			int val = ca.VirtA();
			Test.Assert(val == 931);

			Object obj = ca;

			var ifaceA = obj as ITestA;
			if (ifaceA != null)
				val += ifaceA.MethodA() * 1000;
			var ifaceB = obj as ITestB;
			if (ifaceB != null)
				val += ifaceB.MethodB() * 10000;
			var ifaceC = obj as ITestC;
			if (ifaceC != null)
				val += ifaceC.MethodC() * 100000;
			var ifaceD = obj as ITestD;
			if (ifaceD != null)
				val += ifaceD.MethodD() * 1000000;
			return val;
		}

		public static void Test()
		{
			//Test_Start
			int val0 = TestA();
		}
	}
}
