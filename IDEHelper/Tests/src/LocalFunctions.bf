#pragma warning disable 168

using System;

namespace Tests
{
	class LocalFunctions
	{
		class ClassA<T>
		{
			public int Get<T2>()
			{
				int LocalA()
				{
				   return 123;
				}

				int LocalB<T3>(T3 val) where T3 : IHashable
				{
					val.GetHashCode();
					return 234;
				}

				return LocalA() + LocalB(1.2f);
			}
		}

		[Test]
		static void TestA()
		{
			int a = 1;

			void FuncA()
			{
				void FuncB()
				{
					a += 100;
				}

				FuncB();
			}

			FuncA();
			Test.Assert(a == 101);

			ClassA<int> ca = scope .();
			Test.Assert(ca.Get<int16>() == 357);
		}

		[Test]
		static void TestB()
		{
			int a = 1;
			Action act = scope [&] () =>
			{
				a += 100;
			};
			act();
			Test.Assert(a == 101);
		}

		[Test]
		static void TestC()
		{
			int a = 1;
			const int c = 10;

			Action OuterLocal()
			{
				return new [&] () =>
				{
					void FuncA()
					{
						FuncB();
					}

					void FuncB()
					{
						a += c;
					}

					FuncA();
				};
			}

			var act = OuterLocal();
			act();
			delete act;
			Test.Assert(a == 11);
		}

		[Test]
		static void TestD()
		{
			int a = 1;
			int b = 2;

			void FuncA()
			{
				a += 100;
			}

			mixin MixA(var refVal)
			{
				refVal += 10;
				FuncA();
				MixB!();
			}

			mixin MixB()
			{
				b += 20;
			}

			MixA!(b);
			Test.Assert(a == 101);
			Test.Assert(b == 32);
		}

		[Test]
		static void TestE()
		{
			int a = 1;
			int b = 2;
			int c = 3;

			void FuncA()
			{
				a += 100;
				FuncB();
				b += 200;
			}

			void FuncB()
			{
				c += 300;
			}

			FuncA();
			Test.Assert(a == 101);
			Test.Assert(b == 202);
			Test.Assert(c == 303);
		}

		[Test]
		static void TestF()
		{
			int a = 1;
			const int c = 100;

			Action act = scope [&] () =>
			{
				void FuncA()
				{
					FuncB();

					const int d = 1000;

					void FuncB()
					{
						a += c;
						a += d;
					}
				}

				FuncA();
			};
			act();
			Test.Assert(a == 1101);
		}

		[Test]
		static void TestG()
		{
			int a = 1;

			void FuncA()
			{
				a += 100;

				int a = 2;

				void FuncB()
				{
					a += 1000;
				}

				FuncB();
			}

			FuncA();
			Test.Assert(a == 101);
		}

		[Test]
		public static void TestH()
		{
			int a = 1;

			void FuncA(int b)
			{
				a += 100;
				if (b == 0)
					FuncB();
			}

			void FuncB()
			{
				Action act = scope [&] () =>
				{
					FuncA(1);
				};

				act();
			}
		}

		[Test]
		public static void TestI()
		{
			int a = 1;

			void FuncA()
			{
				a += 100;
			}

			void FuncB()
			{
				int a = 2;
				FuncA();
			}

			FuncB();
			Test.Assert(a == 101);
		}

		class ClassA
		{
			public int mA;

			public mixin MixA()
			{
				mA += 10;

				void LocalA()
				{
					mA += 20;
				}
			}

			public void FuncA()
			{
				MixA!();
			}

			public (int, int) MethodT<T>()
			{
				int a = 8;
				int b = 90;

				int FuncT<T2>()
				{
					int GetIt()
					{
						return b;
					}

					int GetIt<T3>(T3 val)
					{
						return sizeof(T3);
					}
					
					return mA + sizeof(T)*10000 + sizeof(T2)*1000 + GetIt<float>(2.0f)*100 + GetIt() + a;
				}


				int val = FuncT<int8>();
				return (FuncT<int8>(), FuncT<int16>());
			}
		}

		[Test]
		public static void TestJ()
		{
			int a = 8;
			int b = 90;

			int FuncT<T>()
			{
				int GetIt()
				{
					return b;
				}

				int GetIt<T2>(T2 val)
				{
					return sizeof(T2);
				}
											 
  				return sizeof(T)*1000 + GetIt(2.0f)*100 + GetIt() + a;
			}

			Test.Assert(FuncT<int8>() == 1498);
			Test.Assert(FuncT<int16>() == 2498);

			///
			ClassA ca = scope ClassA();
			ca.mA = 300000;
			let res2 = ca.MethodT<int32>();
			Test.Assert(res2.0 == 341498);
			Test.Assert(res2.1 == 342498);
		}

		[Test]
		public static void TestK()
		{
			int a = 8;
			int b = 90;

			int GetIt(Object val)
			{
				return 0;
			}

			int GetIt(int8 val)
			{
				return a + val;
			}

			int GetIt(int16 val)
			{
				return b + val;
			}

			// We want to ensure that each specialization of Get<T> has its own capture list
			int Get<T>(T val)
			{
				return GetIt(val);
			}

			Test.Assert(GetIt((int8)100) == 108);
			Test.Assert(GetIt((int16)100) == 190);
		}

		[Test]
		public static void TestL()
		{
			int a = 8;

			int LocalA()
			{
				return 9;
			}

			int LocalB()
			{
				return a;
			}

			function int() func = => LocalA;
			Test.Assert(func() == 9);

			delegate int() dlg = scope => LocalB;
			Test.Assert(dlg() == 8);
		}
	}
}
