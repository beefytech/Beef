#pragma warning disable 168

namespace IDETest
{
	class HotSwap_Interfaces2
	{
		interface IFaceA
		{
			int Method0();
			/*IFaceA_Method1
			int Method1();
			*/
		}

		class ClassA : IFaceA
		{
			public int Method0()
			{
				return 11;
			}

			/*ClassA_Method1
			public int Method1()
			{
				return 22;
			}
			*/
		}

		static void Test1(IFaceA ia)
		{
			/*Test1_Method1
			int v1 = ia.Method1();
			*/
		}

		public static void Test()
		{
			ClassA ca = scope .();
			IFaceA ia = ca;
			//Test_Start
			int v0 = ia.Method0();
			Test1(ia);
		}
	}
}
