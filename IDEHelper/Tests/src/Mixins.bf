using System;

namespace Tests
{
	class Mixins
	{
		static mixin MixNums(int a, int b)
		{
			(a << 8) | b
		}

		const int cVal = MixNums!(3, 5);

		class MixClass
		{
			public int mA = 100;
			public static int sA = 200;

			public mixin MixA(var addTo)
			{
				mA += addTo;
			}

			public mixin MixB(var addTo)
			{
				void AddIt()
				{
					mA += addTo;
				}

				AddIt();
			}

			public static mixin MixC(var val)
			{
				val + sA
			}
		}

		static mixin GetVal(var a)
		{
			a = 123;
		}

		static mixin GetVal2(out int a)
		{
			a = 234;
		}

		[Test]
		public static void TestBasics()
		{
			MixClass mc = scope MixClass();
			mc.MixA!(10);
			Test.Assert(mc.mA == 110);
			mc.MixB!(10);
			Test.Assert(mc.mA == 120);
			Test.Assert(MixClass.MixC!(30) == 230);
			Test.Assert(cVal == 0x305);

			GetVal!(int val1);
			Test.Assert(val1 == 123);
			GetVal2!(var val2);
			Test.Assert(val2 == 234);

			void CheckStr(char8* cStr)
			{
				Test.Assert(StringView(cStr) == "Test");
			}

			function void(StringView sv) func = (sv) =>
				{
					CheckStr(sv.ToScopeCStr!());
				};
			func("Test");
		}

		[Test]
		public static void TestLocalMixin()
		{
			mixin AppendAndNullify(String str)
			{
				int a = 1;
				a++;
				str.Append("B");
				str = null;
			}

			int a = 2;
			a++;

			String str0 = scope String("A");
			String str1 = str0;

			AppendAndNullify!(str0);
			Test.Assert(str0 == null);
			Test.Assert(str1 == "AB");
		}

	}
}
