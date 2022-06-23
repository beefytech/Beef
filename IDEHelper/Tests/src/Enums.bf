#pragma warning disable 168

using System;

namespace Tests
{
	class Enums
	{
		enum EnumA
		{
			A,
			B
		}

		enum EnumB
		{
			A,
			B = 600
		}

		enum EnumC
		{
			A,
			B = 0x7FFFFFFF
		}

		enum EnumD
		{
			A,
			B = 0x7FFFFFFFFFFFFFFFL
		}

		enum EnumE
		{
			case A;
			case B(int a);
			case C(int a, int b);
		}

		enum EnumF
		{
			case None;
			case EE(EnumE ee);
		}

		enum EnumG
		{
			case A = (.)'A';
			case B = (.)'B';

			public void Set(int val) mut
			{
				this = (.)val;
			}
		}

		[Test]
		static void TestBasic()
		{
			EnumA ea = .A;
			Test.Assert((int)ea == 0);
			Test.Assert(sizeof(EnumA) == 1);
			Test.Assert(sizeof(EnumB) == 2);
			Test.Assert(sizeof(EnumC) == 4);
			Test.Assert(sizeof(EnumD) == 8);
		}

		[Test]
		static void TestPayloads()
		{
			Test.Assert(sizeof(EnumE) == sizeof(int)*2 + 1);
			Test.Assert(sizeof(EnumF) == sizeof(int)*2 + 1 + 1);

			EnumE ee = .C(1, 2);

			int a = 0;
			int b = 0;
			switch (ee)
			{
			case .A:
			case .B(out a):
			case .C(out a, out b):
			}
			Test.Assert(a == 1);
			Test.Assert(b == 2);

			EnumF ef = .EE(.C(3, 4));
			switch (ef)
			{
			case .EE(let eeVal):
				if (!(eeVal case .C(out a, out b)))
				{
					Test.FatalError();
				}
			default:
			}
			Test.Assert(a == 3);
			Test.Assert(b == 4);

			const EnumE e0 = .A;
			const EnumE e1 = .B(1);
		}

		[Test]
		static void TestCases()
		{
			EnumE ee = .A;

			int val = 123;
			bool matches = ee case .B(out val);
			Test.Assert(val == 0);

			// We only assign to 'val' on true here
			val = 123;
			if (ee case .B(out val))
			{
				Test.Assert(val == 0 );
			}
			Test.Assert(val == 123);

			// We always assign to 'val' here because we may enter even if there's no match
			val = 123;
			if ((ee case .B(out val)) || (val > 0))
			{
			}
			Test.Assert(val == 0);

			const EnumA cea = .A;
			switch (cea)
			{
			case .A:
				val = 123;
			}

			ee = .B(10);

			EnumG eg = .A;
			eg.Set(66);
			Test.Assert(eg == .B);

			var ea = EnumA.GetMaxValue();
			Test.Assert(ea == .B);
			Test.Assert(EnumA.GetCount() == 2);
		}
	}
}
