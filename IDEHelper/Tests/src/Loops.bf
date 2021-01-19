#pragma warning disable 168

using System;

namespace Tests
{
	class Loops
	{
		struct StructA
		{
			public int32 mA;
			public int32 mB;
		}

		[Test]
		public static void TestBasics()
		{
			while (false)
			{

			}

			StructA[0] zeroLoop = default;
			for (var val in zeroLoop)
			{
				StructA sa = val;
				int idx = @val;
			}
		}
	}
}
