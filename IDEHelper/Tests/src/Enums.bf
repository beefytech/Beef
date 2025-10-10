#pragma warning disable 4200
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

		[AllowDuplicates]
		public enum EnumJ : uint32
		{
			SDL_BUTTON_LEFT     = 1,
			SDL_BUTTON_LMASK    = (1u << ((SDL_BUTTON_LEFT.Underlying) - 1)),
		}

		[AllowDuplicates]
		public enum EnumK
		{
			SDL_BUTTON_LEFT     = 1,
			SDL_BUTTON_LMASK    = (1u << ((SDL_BUTTON_LEFT.Underlying) - 1)),
		}

		public enum EnumL
		{
			case A;

			public static int operator implicit(Self self);
		}

		public enum EnumM
		{
		    public static implicit operator int(Self self);

		    case A;
		    case B;
		    case C;
		}

		[CRepr]
		public enum EnumN
		{
			A,
			B,
			C,
			D = 4
		}

		public enum EnumO
		{
			case None;
			case EnumN(EnumN n);
			case Delegate(delegate void());
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

			EnumM em = ?;
			int i = em;
			uint u = (uint)em;

			i = 123;
			EnumA e = (EnumA)i;
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
			case (.)default:
				Test.Assert(true);
			default:
				Test.Assert(false);
			}

			ee = .B(123);
			switch (ee)
			{
			case (.)default:
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

			EnumO eo = .Delegate((delegate void()) new () => {});
			if (eo case .Delegate(var dlg))
				delete dlg;

			eo = .EnumN(.A);

			eo = .Delegate(new () => {});
			if (eo case .Delegate(var dlg))
				delete dlg;
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

			Test.Assert((int)EnumJ.SDL_BUTTON_LMASK == 1);
			Test.Assert(typeof(EnumJ).Size == 4);

			Test.Assert((int)EnumK.SDL_BUTTON_LMASK == 1);
			Test.Assert(typeof(EnumK).Size == 8);

			EnumL el = .A;
			Test.Assert(el == 0);
		}

		[Test]
		static void TestCrepr()
		{
			EnumN value = .B;

			Test.Assert(sizeof(EnumN) == sizeof(System.Interop.c_int));

			Test.Assert(value.HasFlag(.A) == true);
			Test.Assert(value.HasFlag(.B) == true);
			Test.Assert(value.HasFlag(.B | .C) == false);
			Test.Assert(value.HasFlag(.D) == false);
			Test.Assert(value.Underlying == 1);

			value = .B | .C;
			Test.Assert(value.HasFlag(.A) == true);
			Test.Assert(value.HasFlag(.B) == true);
			Test.Assert(value.HasFlag(.B | .C) == true);
			Test.Assert(value.HasFlag(.D) == false);
			Test.Assert(value.Underlying == 3);

			ref System.Interop.c_int valueRef = ref value.UnderlyingRef;
			valueRef = 2;
			Test.Assert(value == .C);

			String str = scope String();
			value.ToString(str);

			Test.Assert(str == "C");
		}
	}
}
