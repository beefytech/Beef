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

		enum EnumH { A, B }

		enum EnumI
		{
			case A;case B;case C;case D;case E;
			case F;case G;case H;case I;case J;
			case K;case L;case M;case N;case O;
			case P;case Q;case R;case S;case T;
			case U;case V;case W;case X;case Y;
			case Z;case AA;case AB;case AC;case AD;
			case AE;case AF;case AG;case AH;case AI;
			case AJ;case AK;case AL;case AM;case AN;
			case AO;case AP;case AQ;case AR;case AS;
			case AT;case AU;case AV;case AW;case AX;
			case AY;case AZ;case BA;case BB;case BC;
			case BD;case BE;case BF;case BG;case BH;
			case BI;case BJ;case BK;case BL;case BM;
			case BN;case BO;case BP;case BQ;case BR;
			case BS;case BT;case BU;case BV;case BW;
			case BX;case BY;case BZ;case CA;case CB;
			case CC;case CD;case CE;case CF;case CG;
			case CH;case CI;case CJ;case CK;case CL;
			case CM;case CN;case CO;case CP;case CQ;
			case CR;case CS;case CT;case CU;case CV;
			case CW;case CX;case CY;case CZ;case DA;
			case DB;case DC;case DD;case DE;case DF;
			case DG;case DH;case DI;case DJ;case DK;
			case DL;case DM;case DN;case DO;case DP;
			case DQ;case DR;case DS;case DT;case DU;
			case DV;case DW;case DX;
			case DY(EnumH val);
			// Started from 129 elements, no less
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

			ee = default;
			switch (ee)
			{
			case default:
				Test.Assert(true);
			default:
				Test.Assert(false);
			}

			ee = .B(123);
			switch (ee)
			{
			case default:
				Test.Assert(false);
			default:
				Test.Assert(true);
			}

			switch (ee)
			{
			case .B(100):
				Test.Assert(false);
			case .B(123):
				Test.Assert(true);
			default:
				Test.Assert(false);
			}

			switch (ee)
			{
			case .B(123):
				Test.Assert(true);
			case .B:
				Test.Assert(false);
			default:
				Test.Assert(false);
			}

			switch (ee)
			{
			case .A:
			case .B(123):
				Test.Assert(true);
			case .B:
				Test.Assert(false);
			case .C:
			}

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

			var ea = Enum.GetMaxValue<EnumA>();
			Test.Assert(ea == .B);
			Test.Assert(Enum.GetCount<EnumA>() == 2);

			EnumI ei = .DY(.B);
			bool foundH = false;
			if (ei case .DY(var eh))
				foundH = eh == .B;
			Test.Assert(foundH);
		}
	}
}
