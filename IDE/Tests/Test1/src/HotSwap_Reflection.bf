#pragma warning disable 168

using System;

namespace IDETest
{
	class HotSwap_Reflection
	{
		[Reflect]
		class ClassA
		{
			int mA;//ClassA_mA

			public static Type GetMyType()
			{
				return typeof(ClassA);
			}
		}

		public static void Test()
		{
			//Test_Start
			let t0 = ClassA.GetMyType();
			for (let field in t0.GetFields())
			{
			}
			let t1 = ClassA.GetMyType();
			for (let field in t1.GetFields())
			{
			}
			let t2 = ClassA.GetMyType();
			for (let field in t2.GetFields())
			{
			}
		}
	}
}
