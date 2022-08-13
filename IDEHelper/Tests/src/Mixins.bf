using System;
using System.Collections;

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

		public static mixin CircularMixin<T>(T value)
			where T : var
		{
			10
		}

		public static mixin CircularMixin<K, V>(Dictionary<K, V> value)
			where K : var, IHashable
			where V : var
		{
			
			int total = 0;
			if (value == null)
				total += 1;
			else
			{
				for (let (k, v) in ref value)
				{
					total += CircularMixin!(k);
					total += CircularMixin!(*v);
				}
			}
			total + 100
		}

		static mixin GetRef(var a)
		{
			a += 1000;
			ref a
		}

		static mixin Unwrap(var res)
		{
			res.Value
		}

		static mixin DisposeIt<T>(T val) where T : IDisposable
		{
			val?.Dispose();
		}

		class DispClass : IDisposable
		{
			void IDisposable.Dispose()
			{

			}
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

			Dictionary<int, Dictionary<int, int>> test = scope .() {(1,null)};
			int val = CircularMixin!(test);
			Test.Assert(val == 211);

			DispClass dc = scope .();
			DisposeIt!(dc);
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

			int b = 12;
			GetRef!(b) += 200;
			Test.Assert(b == 1212);

			var c = { ref b };
			c = 99;
			Test.Assert(b == 99);

			Result<StringView> svRes = "ab ";
			var sv2 = Unwrap!(svRes)..Trim();
			Test.Assert(svRes.Value == "ab ");
			Test.Assert(sv2 == "ab");
		}

	}
}
