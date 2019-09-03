#pragma warning disable 168

namespace IDETest
{
	class HotSwap_LocateSym01
	{
		static float UseMod()
		{
			float f = 100.0f;
			/*UseMod_Mod
			f = f % 31.0f;
			*/
			return f;
		}

		public static void Test()
		{
			int a = 0;
			//Test_Start
			float f = UseMod();
		}
	}
}
