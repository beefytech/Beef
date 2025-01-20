using System;
#pragma warning disable 168

namespace IDETest;



class Virtuals02
{
	class ClassA
	{
		public virtual int GetVal01() => 100;
		public
			/*ClassA_GetVal01
			virtual
			*/
			int GetVal02() => 200;
		public virtual int GetVal03() => 300;
	}

	class ClassB : ClassA
	{
		public override int GetVal03()
		{
			return 301;
		}
		public virtual int GetVal04() => 400;
		public
			/*ClassB_GetVal04
			virtual
			*/
			int GetVal05() => 500;
	}

	static void TestFuncs()
	{
		ClassA ca = scope .();
		ClassB cb = scope .();
		int val;

		void** funcs = (.)(void*)(cb.[Friend]mClassVData & ~0xFF);
		int valA1 = ca.GetVal01();
		
		int valA2 = ca.GetVal02();
		int valB1 = cb.GetVal01();
		int valB2 = cb.GetVal02();
		int valB3 = cb.GetVal03();
		int valB4 = cb.GetVal04();
		int valB5 = cb.GetVal05();

		Runtime.Assert(valA1 == 100);
		Runtime.Assert(valA2 == 200);
		Runtime.Assert(valB1 == 100);
		Runtime.Assert(valB2 == 200);
		Runtime.Assert(valB3 == 301);
		Runtime.Assert(valB4 == 400);
		Runtime.Assert(valB5 == 500);
	}

	public static void Test()
	{
		//Test_Start
		TestFuncs();
		TestFuncs();
		TestFuncs();
	}
}