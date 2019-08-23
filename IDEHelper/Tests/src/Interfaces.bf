using System;

namespace Tests
{
	class Interfaces
	{
		interface IFaceA
		{
			int FuncA(int a) mut;
			int FuncA2() mut { return 0; }
		}

		interface IFaceB : IFaceA
		{
			void FuncB(int a) mut
			{
				FuncA(a + 100);
			}
		}

		concrete interface IFaceC
		{
			concrete IFaceA GetConcreteIA();
		}

		struct StructA : IFaceB
		{
			public int mA = 10;

			int IFaceA.FuncA(int a) mut
			{
				mA += a;
				return mA;
			}
		}

		struct StructB : IFaceC
		{
			public StructA GetConcreteIA()
			{
				return StructA();
			}
		}

		static int UseIA<T>(mut T val, int a) where T : IFaceA
		{
			return val.FuncA(a);
		}

		static int UseIA2<T>(mut T val) where T : IFaceA
		{
			return val.FuncA2();
		}

		static void UseIB<T>(mut T val, int a) where T : IFaceB
		{
			val.FuncB(a);
		}

		class ClassA : IFaceA
		{
			public int FuncA(int a)
			{
				return 5;
			}

			public virtual int FuncA2()
			{
				return 50;
			}
		}

		class ClassB : ClassA
		{
			public new int FuncA(int a)
			{
				return 6;
			}

			public override int FuncA2()
			{
				return 60;
			}
		}

		class ClassC : ClassA, IFaceA
		{
			public new int FuncA(int a)
			{
				return 7;
			}

			public override int FuncA2()
			{
				return 70;
			}
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa = .();
			UseIB(mut sa, 9);
			Test.Assert(sa.mA == 119);

			ClassA ca = scope ClassA();
			ClassB cb = scope ClassB();
			ClassA cba = cb;
			ClassC cc = scope ClassC();
			ClassA cca = cc;

			Test.Assert(UseIA(ca, 100) == 5);
			Test.Assert(UseIA(cb, 100) == 5);
			Test.Assert(UseIA(cba, 100) == 5);
			Test.Assert(UseIA((IFaceA)cba, 100) == 5);
			Test.Assert(UseIA(cc, 100) == 7);
			Test.Assert(UseIA(cca, 100) == 5);

			Test.Assert(UseIA2(ca) == 50);
			Test.Assert(UseIA2(cb) == 60);
			Test.Assert(UseIA2(cba) == 60);
			Test.Assert(UseIA2((IFaceA)cba) == 60);
			Test.Assert(UseIA2(cc) == 70);
			Test.Assert(UseIA2(cca) == 70);
		}
	}
}
