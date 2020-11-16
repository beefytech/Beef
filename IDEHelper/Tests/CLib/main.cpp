#include <inttypes.h>
#include <stdio.h>

namespace Tests
{	
	struct Interop
	{
		struct StructA
		{
			int mA;
			static int sVal;

			int MethodA0(int arg0)
			{
				return arg0 + mA * 100;
			}

			StructA MethodA1(StructA other, int arg0)
			{
				StructA ret;
				ret.mA = mA + other.mA + arg0;
				return ret;
			}
		};

		struct StructB
		{
			int mA;
			char mB;

			int MethodB0(int arg0)
			{
				return arg0 + mA * 100 + mB * 10000;
			}

			StructB MethodB1(StructB other, int arg0)
			{
				StructB ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructC
		{
			char mA;
			int mB;

			int MethodC0(int arg0)
			{
				return arg0 + mA * 100 + mB * 10000;
			}

			StructC MethodC1(StructC other, int arg0)
			{
				StructC ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructD
		{
			int mA;
			int mB;

			int MethodD0(int arg0)
			{
				return arg0 + mA * 100 + mB * 10000;
			}

			StructD MethodD1(StructD other, int arg0)
			{
				StructD ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructE
		{
			int mA;
			int mB;
			int mC;

			int MethodE0(int arg0)
			{
				return arg0 + mA * 100 + mC * 10000;
			}

			StructE MethodE1(StructE other, int arg0)
			{
				StructE ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructF
		{
			char mA;
			char mB;
			char mC;

			int MethodF0(int arg0)
			{
				return arg0 + mA * 100 + mC * 10000;
			}

			StructF MethodF1(StructF other, int arg0)
			{
				StructF ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructG
		{
			char mA;
			char mB;
			char mC;
			char mD;

			int MethodG0(int arg0)
			{
				return arg0 + mA * 100 + mC * 10000;
			}

			StructG MethodG1(StructG other, int arg0)
			{
				StructG ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructH
		{
			int64_t mA;
			int64_t mB;
			int64_t mC;			

			int MethodH0(int arg0)
			{
				return arg0 + (int)mA * 100 + (int)mC * 10000;
			}

			StructH MethodH1(StructH other, int arg0)
			{
				StructH ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructI
		{
			char mA;
			char mB;
			char mC;
			char mD;
			char mE;

			int MethodI0(int arg0)
			{
				return arg0 + (int)mA * 100 + (int)mC * 10000;
			}

			StructI MethodI1(StructI other, int arg0)
			{
				StructI ret;
				ret.mA = mA + other.mA + arg0;
				ret.mB = mB + other.mB;
				return ret;
			}
		};

		struct StructJ
		{
			char* mPtr;
			intptr_t mLength;

			int MethodJ0(int arg0)
			{
				return arg0 + (int)mLength * 100;
			}

			StructJ MethodJ1(StructJ other, int arg0)
			{
				StructJ ret;
				ret.mPtr = other.mPtr;
				ret.mLength = other.mLength + arg0;
				return ret;
			}
		};

		struct StructK
		{
			float mX;
			float mY;
		};

		struct StructL
		{
			float mX;
			float mY;
			float mZ;
		};

		struct StructM
		{
			float mX;
			float mY;
			float mZ;
			float mW;
		};

		struct StructN
		{
			float mX;
			float mY;
			float mZ;
			float mW;
			float mU;
		};

		struct StructO
		{
			float mX;
			int mY;
		};

		struct StructP
		{
			float mX;
			float mY;
			int mZ;
		};

		struct StructQ
		{
			float mX;
			float mY;
			int mZ;
			int mW;
		};

		struct StructR
		{
			double mX;
			double mY;
		};

		struct StructS
		{
			float mX;
			double mY;
		};

		struct StructT
		{
			double mX;
			double mY;
			double mZ;
		};

		struct StructU
		{
			StructK mK;
		};

		struct StructV
		{
			int64_t mX;
			short mY;
		};

		struct StructW
		{
			float mX;
		};
	};

	int Interop::StructA::sVal = 1234;
}

using namespace Tests;

extern "C" int Func0(int a, int b)
{	
	return a + b * 100;
}

extern "C" int Func0K(int a, Interop::StructK b)
{
	//printf("Func0K: %d %f %f\n", a, b.mX, b.mY);
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0L(int a, Interop::StructL b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0M(int a, Interop::StructM b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0N(int a, Interop::StructN b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0O(int a, Interop::StructO b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0P(int a, Interop::StructP b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0Q(int a, Interop::StructQ b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0R(int a, Interop::StructR b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0S(int a, Interop::StructS b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0T(int a, Interop::StructT b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0U(int a, Interop::StructU b)
{
	return a + (int)b.mK.mX * 100 + (int)b.mK.mY * 10000;
}

extern "C" int Func0V(int a, Interop::StructV b)
{
	return a + (int)b.mX * 100 + (int)b.mY * 10000;
}

extern "C" int Func0W(int a, Interop::StructW b)
{
	return a + (int)b.mX * 100;
}

extern "C" int Func0KM(Interop::StructK a, Interop::StructM b, Interop::StructK c)
{
	return (int)a.mX + (int)b.mX * 100 + (int)c.mX * 1000;
}

//////////////////////////////////////////////////////////////////////////

extern "C" int Func1A(Interop::StructA arg0, Interop::StructA arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1B(Interop::StructB arg0, Interop::StructB arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1C(Interop::StructC arg0, Interop::StructC arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1D(Interop::StructD arg0, Interop::StructD arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1E(Interop::StructE arg0, Interop::StructE arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1F(Interop::StructF arg0, Interop::StructF arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1G(Interop::StructG arg0, Interop::StructG arg1, int arg2)
{
	return arg0.mA + arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1H(Interop::StructH arg0, Interop::StructH arg1, int arg2)
{
	return (int)arg0.mA + (int)arg1.mA * 100 + arg2 * 10000;
}

extern "C" int Func1I(Interop::StructI arg0, Interop::StructI arg1, int arg2)
{
	return (int)arg0.mA + (int)arg1.mA * 100 + arg2 * 10000;
}

//////////////////////////////////////////////////////////////////////////

extern "C" Interop::StructA Func2A(Interop::StructA arg0, int arg1)
{
	Interop::StructA ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructB Func2B(Interop::StructB arg0, int arg1)
{
	Interop::StructB ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructC Func2C(Interop::StructC arg0, int arg1)
{
	Interop::StructC ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructD Func2D(Interop::StructD arg0, int arg1)
{
	Interop::StructD ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructE Func2E(Interop::StructE arg0, int arg1)
{
	Interop::StructE ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructF Func2F(Interop::StructF arg0, int arg1)
{
	Interop::StructF ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructG Func2G(Interop::StructG arg0, int arg1)
{
	Interop::StructG ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructH Func2H(Interop::StructH arg0, int arg1)
{
	Interop::StructH ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

extern "C" Interop::StructI Func2I(Interop::StructI arg0, int arg1)
{
	Interop::StructI ret;
	ret.mA = arg0.mA + arg1;
	return ret;
}

//////////////////////////////////////////////////////////////////////////

extern "C" int Func3A(Interop::StructA* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3B(Interop::StructB* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3C(Interop::StructC* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3D(Interop::StructD* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3E(Interop::StructE* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3F(Interop::StructF* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

extern "C" int Func3G(Interop::StructG* ptr)
{
	return ptr[0].mA + ptr[1].mA * 100;
}

//////////////////////////////////////////////////////////////////////////

extern "C" Interop::StructJ Func4J(Interop::StructJ arg0, Interop::StructJ arg1, Interop::StructJ arg2, Interop::StructJ arg3)
{
	Interop::StructJ ret;
	ret.mPtr = arg0.mPtr;
	ret.mLength = arg0.mLength + arg1.mLength * 100 + arg2.mLength * 10000 + arg3.mLength * 1000000;
	return ret;
}

void UseIt()
{
	Interop::StructA sa;
	sa.MethodA0(0);
	sa.MethodA1(sa, 1);
	Interop::StructB sb;
	sb.MethodB0(0);
	sb.MethodB1(sb, 1);
	Interop::StructC sc;
	sc.MethodC0(0);
	sc.MethodC1(sc, 1);
	Interop::StructD sd;
	sd.MethodD0(0);
	sd.MethodD1(sd, 1);
	Interop::StructE se;
	se.MethodE0(0);
	se.MethodE1(se, 1);
	Interop::StructF sf;
	sf.MethodF0(0);
	sf.MethodF1(sf, 1);
	Interop::StructG sg;
	sg.MethodG0(0);
	sg.MethodG1(sg, 1);
	Interop::StructH sh;
	sh.MethodH0(0);
	sh.MethodH1(sh, 1);
	Interop::StructI si;
	si.MethodI0(0);
	si.MethodI1(si, 1);
	Interop::StructJ sj;
	sj.MethodJ0(0);
	sj.MethodJ1(sj, 1);
}