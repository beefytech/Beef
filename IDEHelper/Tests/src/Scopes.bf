using System;
using System.Diagnostics;

namespace Tests
{
	class Scopes
	{
		[Test]
		public static void TestRestore()
		{
			// This shouldn't overflow because we should restore the stack between 'i' iterations
			for (int i < 100)
			IBlock:
			{
				// Allocate 500kb
				for (int j < 5)
				{
				   scope:IBlock uint8[100*1024];
				}
			}
		}

		[Test]
		public static void TestGetStrScope()
		{
			var str = GetStr!();
			Test.Assert(str == "TestString");

			{
				str = GetStr!::();
			}
			Test.Assert(str == "TestString");
		}

		/*[Test(ShouldFail=true)]
		public static void TestFailGetStrScope()
		{
			String str;
			{
				str = GetStr!();
			}
			// Should detect as deleted
#unwarn
			str.Contains('T');
		}*/

		static int sVal = 123;

		struct DisposableInstance : int32
		{
			public void Dispose() mut
			{
				sVal++;
			}
		}

		DisposableInstance sDisposableInstance = (.)123;

		static Result<DisposableInstance> GetDisposable(StringView profileDesc = default, int sampleRate = -1)
		{
			return DisposableInstance();
		}

		public static void Defer0(ref int val)
		{
			for (int i < 10)
			{
				defer::
				{
					val += 100;
				}

				if (i == 2)
				{
					if (GetDisposable() case .Ok(var sampInst))
					{
						defer:: sampInst.Dispose();
					}

					return;
				}

				defer::
				{
					val++;
				}
			}
		}

		[Test]
		public static void TestBasics()
		{
			int a = 0;
			Defer0(ref a);
			Test.Assert(a == 302);
			Test.Assert(sVal == 124);
		}

		public static mixin GetStr()
		{
			scope:mixin String("TestString")
		}

		class ClassA
		{
			public int mA = 123;
			public static int sAllocCount = 0;

			public this()
			{
				sAllocCount++;
			}

			public ~this()
			{
				Test.Assert(mA == 123);
				sAllocCount--;
			}
		}

		class ClassB
		{
			public int mA = 234;
			public static int sAllocCount = 0;

			public this()
			{
				sAllocCount++;
			}

			public ~this()
			{
				Test.Assert(mA == 234);
				sAllocCount--;
			}
		}

		static bool CheckTrue(Object obj)
		{
			return true;
		}

		static bool CheckFalse(Object obj)
		{
			return false;
		}

		[Test]
		public static void TestIf()
		{
			//
			{
				if ((CheckTrue(scope ClassA())) || (CheckTrue(scope ClassB())))
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 0);	  
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}

			//
			{
				if ((CheckFalse(scope ClassA())) || (CheckTrue(scope ClassB())))
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 1);
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}

			//
			{
				if ((CheckFalse(scope ClassA())) || (CheckFalse(scope ClassB())))
				{
					Test.FatalError();
				}
				else
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 1);
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}

			//
			{
				if ((CheckTrue(scope ClassA())) && (CheckTrue(scope ClassB())))
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 1);
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}

			//
			{
				if ((CheckTrue(scope ClassA())) && (CheckFalse(scope ClassB())))
				{
					Test.FatalError();
				}
				else
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 1);
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}

			//
			{
				if ((CheckFalse(scope ClassA())) && (CheckFalse(scope ClassB())))
				{
					Test.FatalError();
				}
				else
				{
					Test.Assert(ClassA.sAllocCount == 1);
					Test.Assert(ClassB.sAllocCount == 0);
				}
				Test.Assert(ClassA.sAllocCount == 0);
				Test.Assert(ClassB.sAllocCount == 0);
			}
		}
	}
}
