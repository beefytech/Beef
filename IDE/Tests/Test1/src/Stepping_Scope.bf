#pragma warning disable 168

namespace IDETest
{
	class Stepping_Scope
	{
		class ClassA
		{
			public ~this()
			{
				int b = 99;
			}
		}

		static void Test1(int inVal)
		{
			int a = 1;
			var ca = scope ClassA();

			if (inVal == 0)
			{
				a = 2;
				return;
			}

			a = 3;
			//Stepping_Scope_Test1_Leave
		}

		static void Test2(int inVal)
		{
			int a = 1;
			
			if (inVal == 0)
			{
				var ca = scope:: ClassA();
				a = 2;
				return;
			}

			a = 3;
			//Stepping_Scope_Test1_Leave
		}

		public static void Test()
		{
			//Stepping_Scope_Test_Start
			Test1(0);
			Test1(1);

			Test2(0);
			Test2(1);
		}
	}
}
