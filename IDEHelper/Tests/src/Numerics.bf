using System;
using System.Numerics;

namespace Tests
{
	class Numerics
	{
		[Test, UseLLVM]
		public static void TestBasics()
		{
			float4 v0 = .(1, 2, 3, 4);
			float4 v1 = .(10, 100, 1000, 10000);

			float4 v2 = v0 * v1;
			Test.Assert(v2 === .(10, 200, 3000, 40000));
			Test.Assert(v2 !== .(10, 200, 3000, 9));
			Test.Assert(v2.x == 10);
			Test.Assert(v2.y == 200);
			Test.Assert(v2.z == 3000);
			Test.Assert(v2.w == 40000);

			float4 v3 = v0.wzyx;
			Test.Assert(v3 === .(4, 3, 2, 1));
		}
	}
}
