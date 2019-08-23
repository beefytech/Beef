namespace IDETest
{
	struct SplatA
	{
		public int mA;
	}

	struct SplatB : SplatA
	{
		public float mB;
	}

	struct SplatC : SplatB
	{
		public char8 mC;

		public void DoTest()
		{

		}

		public float GetB()
		{
			return mB;
		}
	}

	class SplatTester
	{
		public static int GetC(SplatC sc)
		{
			return sc.mA;
		}

		public static void Test()
		{
			SplatC sc = .();
			sc.mA = 123;
			sc.mB = 1.2f;
			sc.mC = 'Z';

			//SplatTester_SplatC_Call
			sc.DoTest();
			sc.GetB();
			GetC(sc);
		}
	}
}
