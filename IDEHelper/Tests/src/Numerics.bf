#pragma warning disable 168

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

			Result<uint16> r0 = 123;
			Result<int16> r1 = 2000;
			Result<uint32> r2 = 3000;

			uint16 v4 = r0 + 123;
			var v5 = r0 + 2;
			Test.Assert(v5.GetType() == typeof(uint16));
			var v6 = r0 + r0;
			Test.Assert(v6.GetType() == typeof(uint16));
			var v7 = r0 + r1;
			Test.Assert(v7.GetType() == typeof(int));
			var v8 = r0 + r2;
			Test.Assert(v8.GetType() == typeof(uint32));
			var v9 = r2 + r0;
			Test.Assert(v9.GetType() == typeof(uint32));
		}
	}
}
