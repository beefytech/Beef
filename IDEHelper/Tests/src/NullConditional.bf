#pragma warning disable 168

using System;

namespace Tests
{
	class CondB
	{
		public int mInt = 123;
		public String mStr;

		public int Val
		{
			get
			{
				 return 234;
			}
		}

		public int GetVal()
		{
			return 345;
		}

		public CondB SelfProp
		{
			get
			{
				return this;
			}
		}

		public CondB GetSelf(int val)
		{
			return (val > 0) ? this : null;
		}

		public StringView StrView
		{
			get
			{
				return "CondB";
			}
		}
	}

	class CondA
	{
		public CondB mCondB;
		public CondB mCondB2;

		CondB CondBVal
		{
			get
			{
				return mCondB;
			}
		}

		CondB GetCondB()
		{
			return mCondB;
		}

		public CondB CondBProp
		{
			get
			{
				return mCondB;
			}
		}
	}

	class NullConditional
	{
		[Test]
		static void TestBasic()
		{
			CondA ca = scope CondA();
			ca.mCondB = scope CondB();
			if (int i = ca?.mCondB?.mInt)
				Test.Assert(i == 123);
			else
				Test.FatalError();

			Test.Assert(ca?.mCondB?.mStr == null);
			Test.Assert(ca?.mCondB?.mStr?.Length != 0);
			Test.Assert(!(ca?.mCondB?.mStr?.Length == 0));

			if (let i = ca?.mCondB?.mInt)
			{
				Test.Assert(typeof(decltype(i)) == typeof(int));
				Test.Assert(i == 123);
			}
			else
				Test.FatalError();

			var i2 = ca?.mCondB?.Val;
			Test.Assert(i2.Value == 234);

			var i3 = ca?.mCondB?.GetVal();
			Test.Assert(i3.Value == 345);

			if (int i4 = ca?.mCondB2?.mInt)
			{
				Test.FatalError();
			}
		}

		[Test]
		static void TestParen()
		{
			CondA ca = scope CondA();
			ca.mCondB = scope CondB();

			let i = (ca?.mCondB?.mInt).GetValueOrDefault();
			Test.Assert(i == 123);

			let i2 = (ca?.mCondB2?.mInt).GetValueOrDefault();
			Test.Assert(i2 == 0);
		}

		static int GetOne()
		{
			return 1;
		}

		[Test]
		static void TestSplitBlocks()
		{
			CondA ca = scope CondA();
			ca.mCondB = scope CondB();
			bool b = ca.mCondB.mInt > 0;

			// Evaluating a link can end in a different block than it started in, such as with a ternary argument
			CondB cb = ca?.mCondB?.GetSelf(b ? GetOne() : 2)?.GetSelf(1);
			Test.Assert(cb == ca.mCondB);
			cb = ca?.mCondB?.GetSelf(b ? -GetOne() : 2)?.GetSelf(1);
			Test.Assert(cb == null);
			cb = ca?.mCondB2?.GetSelf(b ? GetOne() : 2)?.GetSelf(1);
			Test.Assert(cb == null);

			// Optimized object access checks split blocks too
			cb = ca?.CondBProp?.SelfProp;
			Test.Assert(cb == ca.mCondB);

			// Only the lhs can be cast here, which gets emitted in a separate block after the rhs
			StringView sv = cb.mStr ?? cb.StrView;
			Test.Assert(sv == "CondB");
			cb.mStr = scope String("Str");
			sv = cb.mStr ?? cb.StrView;
			Test.Assert(sv == "Str");

			ca.mCondB = null;
			cb = ca?.CondBProp?.SelfProp;
			Test.Assert(cb == null);
		}
	}
}
