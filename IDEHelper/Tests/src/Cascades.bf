using System;

namespace Tests
{
	class Cascades
	{
		struct StructA
		{
			public static int sDiscardCount;
			public void ReturnValueDiscarded()
			{
				sDiscardCount++;
			}

			public void HandleResult()
			{

			}

			public StructA Method0(int a)
			{
				return .();
			}
		}

		static StructA MethodA(int a, float b)
		{
			return .();
		}

		static StructA MethodB(int a, out float b)
		{
			b = 100;
			return .();
		}

		[Test]
		public static void TestBasics()
		{
			MethodA(1, 1.2f);
			Test.Assert(StructA.sDiscardCount == 1);

			StructA sa = .();
			sa.Method0(1)..Method0(2).HandleResult();
			Test.Assert(StructA.sDiscardCount == 2);

			int a = MethodA(.. 12, 2.3f);
			Test.Assert(StructA.sDiscardCount == 3);
			Test.Assert(a == 12);

			var (b, c) = MethodA(.. 12, .. 2.3f);
			Test.Assert(StructA.sDiscardCount == 4);
			Test.Assert(b == 12);
			Test.Assert(c == 2.3f);
			var d = MethodA(.. 12, .. 2.3f);
			Test.Assert(StructA.sDiscardCount == 5);
			Test.Assert(d == (12, 2.3f));
			var f = ref MethodB(12, .. var e);
			Test.Assert(StructA.sDiscardCount == 6);
			e += 23;
			Test.Assert(e == (int)123);
			Test.Assert(f == (int)123);
		}
	}
}
