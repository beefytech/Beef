#pragma warning disable 168

using System;

namespace Tests
{
	class Functions
	{
		class ClassA
		{
			public int mA = 123;

			public int GetA(float f)
			{
				return mA + (int)f;
			}

			public virtual int GetB(float f)
			{
				return mA + (int)f + 1000;
			}

			public int GetT<T>(T val) where T : var
			{
				return mA + (int)val;
			}
		}

		class ClassB : ClassA
		{
			public override int GetB(float f)
			{
				return mA + (int)f + 2000;
			}
		}

		struct StructA
		{
			int mA = 123;
			int mB = 234;

			public this()
			{

			}

			public this(int a, int b)
			{
				mA = a;
				mB = b;
			}

			public int GetA(float f)
			{
				return mA + mB*100 + (int)f;
			}

			public int GetA2(float f) mut
			{
				return mA + mB*100 + (int)f;
			}

			public StructA GetA3(float f)
			{
				StructA sa;
				sa.mA = mA + 1000;
				sa.mB = mB + 2000 + (int)f;
				return sa;
			}

			public StructA GetA4(float f) mut
			{
				StructA sa;
				sa.mA = mA + 1000;
				sa.mB = mB + 2000 + (int)f;
				return sa;
			}
		}

		struct StructB
		{
			function StructB Func(StructB this, float f);
			function StructB FuncMut(mut StructB this, float f);

			int mA = 123;
			int mB = 234;
			int mC = 345;

			public this()
			{

			}

			public this(int a, int b, int c)
			{
				mA = a;
				mB = b;
				mC = c;
			}

			public int GetA(float f)
			{
				return mA + mB*100 + (int)f;
			}

			public int GetA2(float f) mut
			{
				return mA + mB*100 + (int)f;
			}

			public StructB GetA3(float f)
			{
				StructB sb;
				sb.mA = mA + 1000;
				sb.mB = mB + 2000 + (int)f;
				sb.mC = mC + 3000;
				return sb;
			}

			public StructB GetA4(float f) mut
			{
				StructB sb;
				sb.mA = mA + 1000;
				sb.mB = mB + 2000 + (int)f;
				sb.mC = mC + 3000;
				return sb;
			}

			public static void Test()
			{
				StructB sb = .();

				Func func0 = => GetA3;
				Test.Assert(func0(sb, 100.0f) == .(1123, 2334, 3345));
				function StructB (StructB this, float f) func1 = => GetA3;

				Test.Assert(func0 == func1);
				func0 = func1;

				FuncMut func2 = => GetA4;
				Test.Assert(func2(sb, 100.0f) == .(1123, 2334, 3345));
				function StructB (mut StructB this, float f) func3 = => GetA4;
			}
		}

		[CRepr]
		struct StructCRepr
		{
			public int32 something = 1;

			bool Run() => something == 1;

			public static void Test()
			{
				StructCRepr sc = .();
				function bool(StructCRepr this) func = => Run;

				Test.Assert(func(sc));
			}
		}

		interface ITest
		{
			static void Func();
		}

		class ClassC<T> where T : ITest
		{
			static function void() func = => T.Func;

			public static void Test()
			{
				func();
			}
		}

		class Zoop : ITest
		{
			public static int sVal;

			public static void Func()
			{
				sVal = 123;
			}
		}

		public static int UseFunc0<T>(function int (T this, float f) func, T a, float b)
		{
			return func(a, b);
		}

		public static int UseFunc1<T>(function int (mut T this, float f) func, mut T a, float b)
		{
			return func(a, b);
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope ClassA();
			ClassA ca2 = scope ClassB();

			StructA sa = .();
			StructB sb = .();

			function int (ClassA this, float f) func0 = => ca.GetA;
			function int (ClassA this, float) func0b = func0;
			Test.Assert(func0(ca, 100.0f) == 223);
			func0 = => ca.GetT<float>;
			Test.Assert(func0(ca, 100.0f) == 223);

			func0 = => ca.GetB;
			Test.Assert(func0(ca, 100.0f) == 1223);
			func0 = => ca2.GetB;
			Test.Assert(func0(ca, 100.0f) == 2223);
			func0 = => ClassA.GetB;
			Test.Assert(func0(ca, 100.0f) == 1223);
			func0 = => ClassB.GetB;
			Test.Assert(func0(ca, 100.0f) == 2223);

			func0 = => ca.GetA;

			function int (StructA this, float f) func1 = => sa.GetA;
			var func1b = func1;
			Test.Assert(func1(sa, 100.0f) == 23623);
			Test.Assert(!
				[IgnoreErrors(true)]
				{
					func1b = => sb.GetA;
					true
				});

			function int (mut StructA this, float f) func2 = => sa.GetA2;
			Test.Assert(func2(sa, 100.0f) == 23623);

			function StructA (StructA this, float f) func3 = => sa.GetA3;
			Test.Assert(func3(sa, 100.0f) == .(1123, 2334));

			function StructA (mut StructA this, float f) func4 = => sa.GetA4;
			Test.Assert(func4(sa, 100.0f) == .(1123, 2334));

			StructB.Test();

			Test.Assert(UseFunc0(func0, ca, 100.0f) == 223);
			Test.Assert(UseFunc0(func1, sa, 100.0f) == 23623);
			Test.Assert(!
				[IgnoreErrors(true)]
				{
					UseFunc0(func2, sa, 100.0f);
					true
				});

			StructCRepr.Test();

			ClassC<Zoop>.Test();
			Test.Assert(Zoop.sVal == 123);
		}
	}
}
