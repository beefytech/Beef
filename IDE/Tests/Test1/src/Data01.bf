using System;

#pragma warning disable 168

namespace IDETest
{
	class Data01
	{
		struct Base
		{
			int32 mA;
			int64 mB;
		}

		struct Derived : Base
		{
			int8 mC;
		}

		public static void Test()
		{
			//Test_Start
			Derived dr = .();
			Int iVal = (.)123;

			int q = 999;

			//Test_End
		}
	}
}
