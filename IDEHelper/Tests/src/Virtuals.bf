#pragma warning disable 168

using System;

namespace Tests
{
	class Virtuals
	{
		class ClassA
		{
			public virtual int Method0(int a = 111)
			{
				return a;
			}
		}

		class ClassB : ClassA
		{
			public override int Method0(int a = 222)
			{
				return a + 1000;
			}
		}

		class ClassC : ClassB
		{
			public override int Method0(int a)
			{
				return a + 2000;
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope ClassA();
			Test.Assert(ca.Method0() == 111);

			ClassB cb = scope ClassB();
			Test.Assert(cb.Method0() == 1222);

			cb = scope ClassC();
			Test.Assert(cb.Method0() == 2222);
		}

		// A virtual property accessor reached only through 'base' used to get a declaration
		//  without ever being reified, which failed at link time
		class PropBase
		{
			protected int mVal;

			protected virtual int Val
			{
				get => mVal;
				set { mVal = value; }
			}

			protected virtual int this[int idx]
			{
				get => mVal + idx;
				set { mVal = value + idx; }
			}

			public int RawVal => mVal;
		}

		class PropMid : PropBase
		{
			protected override int Val
			{
				get => base.Val;
				set { base.Val = value + 1; }
			}
		}

		class PropDerived : PropMid
		{
			public this()
			{
				base.Val = 10;
			}

			public int GetBaseVal() => base.Val;

			public void SetBaseIdx(int val)
			{
				base[2] = val;
			}

			public int GetBaseIdx() => base[3];
		}

		[Test]
		public static void TestBaseProperties()
		{
			PropDerived pd = scope PropDerived();
			Test.Assert(pd.RawVal == 11);
			Test.Assert(pd.GetBaseVal() == 11);

			pd.SetBaseIdx(20);
			Test.Assert(pd.RawVal == 22);
			Test.Assert(pd.GetBaseIdx() == 25);
		}
	}
}
