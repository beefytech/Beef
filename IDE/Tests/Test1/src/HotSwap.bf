using System.Diagnostics;
using System;

#pragma warning disable 168

namespace IDETest
{
	interface IHot
	{
		int IHotA();	
		/*IHot_IHotB
		int IHotB();
		*/
	}

	class HotA : IHot
	{
		public virtual int MethodA()
		{			
			return 10;
		}

		//*HotA_MethodB
		public virtual int MethodB()
		{
			return 11;
		}
		/*@*/
		
		/*HotA_MethodC
		public virtual int MethodC()
		{
			return 12;
		}
		*/

		// Note- this is purposely beneath MethodC
		/*HotA_MethodB_2
		public virtual int MethodB()
		{
			return 111;
		}
		*/

		/*HotA_MethodD
		public virtual int MethodD()
		{
			return 13;
		}
		*/

		public virtual int MethodE()
		{
			return 14;
		}

		public int IHotA()
		{
			return 15;
		}

		public int IHotB()
		{
			return 16;
		}
	}

	class HotB : HotA
	{
		public override int MethodA()
 		{
 			//HotB_MethodA_Return
			return 20;
		}

		/*HotB_MethodB_Start
		public override int MethodB()
		{
			return 21;
		}
		*/		

		/*HotB_MethodC_Start
		public override int MethodC()
		{
			return 22;
		}
		*/		
	}

	class HotTester	
	{
		
		public void Test2(HotA hot)
		{			
			/*HotTester_Test2
			int val = hot.MethodC();
			*/
		}		
		
		public void Test3(HotA hot)
		{
			/*HotTester_Test3
			int val = hot.MethodE();
			*/
		}

		public void Test1()
		{
			//*HotTester_Test1
			
			HotA hot = scope HotB();

			//HotStart
			int val = hot.MethodA();
			val = hot.MethodB();
			val = hot.MethodB();

			Test2(hot);

			//HotTester_Test1_EndTest
			val = 99;

			Test3(hot);

			/*@*/
		}		

		public void TestVirtualRemap2(HotA hot)
		{
			/*HotTester_TestVirtualRemap2_MethodCCall
			int val = hot.MethodC();
			*/
		}

		public void TestVirtualRemap()
		{
			//HotStart_VirtualRemap
			HotA hot = scope HotB();

			//*HotTester_TestVirtualRemap_MethodBCall
			int val = hot.MethodB();
			/*@*/

			TestVirtualRemap2(hot);
			
			//*HotTester_TestVirtualRemap_MethodBCall_2
			val = hot.MethodB();
			/*@*/
		}

		static public int TestFuncs_Func()
		{
			//HotTester_TestFuncs_Func_Return
			return 123;
		}

		/*HotTester_TestFuncs_Func2
		static public int TestFuncs_Func2()
		{
			//HotTester_TestFuncs_Func2_Return
			return 444;
		}
		*/

		public void TestFuncs_Compare(HotA hot, delegate int() dlgA0, delegate int() dlgT0, function int() funcT0)
		{
			delegate int() dlgA1 = scope => hot.MethodA;
			delegate int() dlgT1 = scope => TestFuncs_Func;
			function int() funcT1 = => TestFuncs_Func;

			Debug.Assert(Delegate.Equals(dlgA0, dlgA1));
			Debug.Assert(Delegate.Equals(dlgT0, dlgT1));
			Debug.Assert(funcT0 == funcT1);

			//TestFuncs_Compare_Calls
			int val = dlgA0();
			val = dlgT0();
			val = funcT0();
		}

		public void TestFuncs()
		{
			//HotStart_Funcs
			HotA hot = scope HotB();
			//Debug.WriteLine("Result: {0}", hot);

			delegate int() dlgA0 = scope => hot.MethodA;
			delegate int() dlgT0 = scope => TestFuncs_Func;
			function int() funcT0 = => TestFuncs_Func;
			TestFuncs_Compare(hot, dlgA0, dlgT0, funcT0);			
		}

		public void TestFuncs2_Compare(function int() funcT0)
		{
			/*TestFuncs2_Compare_Body
			function int() funcT1 = => TestFuncs_Func2;
			Debug.Assert(funcT1 == funcT0);
			*/
			int val = funcT0();
		}

		public void TestFuncs2()
		{
			/*TestFuncs2_Body
			function int() funcT0 = => TestFuncs_Func2;
			TestFuncs2_Compare(funcT0);
			*/
		}

		public void TestIHotA(HotA hot)
		{
			/*HotTester_TestIHotA
			IHot ihot = hot;
			int val = ihot.IHotA();
			*/
		}

		public void TestIHotB(HotA hot)
		{
			/*HotTester_TestIHotB
			IHot ihot = hot;
			int val = ihot.IHotB();
			*/
		}

		public void TestInterfaces()
		{
			//HotStart_Interfaces
			HotA hot = scope HotB();
			TestIHotA(hot);
			TestIHotB(hot);
		}

		public static void Test()
		{
			//Test_Start
			HotTester ht = scope .();
			ht.Test1();
			ht.Test1();

			ht.TestVirtualRemap();
			ht.TestFuncs();
			ht.TestFuncs2();
			ht.TestInterfaces();
		}
	}
}
