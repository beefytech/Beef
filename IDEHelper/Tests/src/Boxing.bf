using System;

namespace Tests
{
	class Boxing
	{
		interface IFace
		{
			int Get() mut;
		}

		struct Struct : IFace
		{
			public int mA = 100;
			public int mB = 200;

			public int Get() mut
			{
				mA += 1000;
				return mA;
			}
		}

		[Test]
		public static void TestBasics()
		{
			Struct val0 = .();
			Object obj0 = val0;
			IFace iface0 = (IFace)obj0;
			Test.Assert(iface0.Get() == 1100);
			Test.Assert(val0.mA == 100); // This should copy values

			/*Struct val1 = .();
			Object obj1 = (Object)(ref val1);
			IFace iface1 = (IFace)obj1;
			Test.Assert(iface1.Get() == 1100);
			Test.Assert(val1.mA == 1100); // This should reference values*/
		}
	}
}
