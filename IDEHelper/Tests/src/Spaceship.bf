using System;

namespace Tests
{
	class Spaceship
	{
		const bool sLess0 = 1.0 <=> 2.0 < 0;

		[Test]
		public static void TestBasics()
		{
			Test.Assert(1 <=> 2 < 0);
			Test.Assert(1.0 <=> 2.0 < 0);

			int8 i8a = -120;
			int8 i8b = 100;

			Test.Assert(i8a <=> i8b < 0);
		}
	}
}
