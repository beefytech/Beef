#pragma warning disable 168

using System;

namespace Tests
{
	class CondB
	{
		public int mInt = 123;

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
	}
}
