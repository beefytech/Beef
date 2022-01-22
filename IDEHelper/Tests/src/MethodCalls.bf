using System;
namespace Tests
{
	class MethodCalls
	{
		struct StructA
		{
			public int mA = 123;
			public int mB = 234;

			public this()
			{

			}

			public this(int a, int b)
			{
				mA = a;
				mB = b;
			}
		}

		struct StructB
		{
			public int mA = 10;
			public int mB = 20;
			public int mC = 30;
			public int mD = 40;

			public this(int a, int b, int c, int d)
			{
				mA = a;
				mB = b;
				mC = c;
				mD = d;
			}
		}

		static int Method0(StructA val)
		{
			return val.mA;
		}

		static int Method1(StructA val, StructA val2)
		{
			return val2.mA;
		}

		static int Method2(StructA val, StructA val2, StructA val3)
		{
			return val3.mA;
		}

		static StructA Method3(StructA val)
		{
			return val;
		}

		static StructA Method4(StructA val, StructA val2)
		{
			return val2;
		}

		static StructA Method5(StructA val, StructA val2, StructA val3)
		{
			return val3;
		}

		int Method0b(StructA val)
		{
			return val.mA;
		}

		int Method1b(StructA val, StructA val2)
		{
			return val2.mA;
		}

		int Method2b(StructA val, StructA val2, StructA val3)
		{
			return val3.mA;
		}

		StructA Method3b(StructA val)
		{
			return val;
		}

		StructA Method4b(StructA val, StructA val2)
		{
			return val2;
		}

		StructA Method5b(StructA val, StructA val2, StructA val3)
		{
			return val3;
		}

		public static float AddFloats<C>(params float[C] vals) where C : const int
		{
			float total = 0;
			for (var val in vals)
				total += val;
			return total;
		}

		struct Valueless
		{
		}

		public static void InCallPtr(in char8* val)
		{
		}

		public static void InCallObj(in Object val)
		{
		
		}

		public static void InCallValueless(in Valueless val)
		{
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa = .(100, 101);
			StructA sa2 = .(200, 201);
			StructA sa3 = .(300, 301);

			Test.Assert(Method0(sa) == 100);
			Test.Assert(Method1(sa, sa2) == 200);
			Test.Assert(Method2(sa, sa2, sa3) == 300);
			Test.Assert(Method3(sa) == sa);
			Test.Assert(Method4(sa, sa2) == sa2);
			Test.Assert(Method5(sa, sa2, sa3) == sa3);

			MethodCalls self = scope .();
			Test.Assert(self.Method0b(sa) == 100);
			Test.Assert(self.Method1b(sa, sa2) == 200);
			Test.Assert(self.Method2b(sa, sa2, sa3) == 300);
			Test.Assert(self.Method3b(sa) == sa);
			Test.Assert(self.Method4b(sa, sa2) == sa2);
			Test.Assert(self.Method5b(sa, sa2, sa3) == sa3);

			Test.Assert(AddFloats(1.0f, 2, 3) == 6.0f);

			InCallPtr("ABC");
			InCallObj("Hey");
			Valueless valueless;
			InCallValueless(valueless);
		}
	}
}
