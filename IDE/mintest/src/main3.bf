//GORB
#pragma warning disable 168

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

//#define A
//#define B

struct StructA
{
	public int mA;

	public static StructA operator+(StructA lhs, float rhs)
	{
		StructA newVal = .();
		newVal.mA = lhs.mA + (int)rhs;
		return newVal;
	}
}

struct StructB
{
	public int mA;

	public static bool operator==(StructA lhs, StructB rhs)
	{
		return lhs.mA == rhs.mA;
	}
}


struct StructC
{
	public int mA;

	public static operator StructD(StructC val)
	{
		StructD conv;
		conv.mA = val.mA;
		return conv;
	}
}

struct StructD
{
	public int mA;

	public static operator StructD(StructC val)
	{
		StructC conv;
		conv.mA = val.mA;
		return conv;
	}
}

struct StructE
{
	public int mA;

	public static operator StructD(StructE val)
	{
		StructC conv;
		conv.mA = val.mA;
		return conv;
	}
}

class ClassA
{
	public int mA;
}

struct StructK
{

}

struct StructL : StructK
{
	public int mA;
}

struct Checker
{
	public static int CheckIt(int* iPtr, int len)
	{
		int acc = 0;
		for (int i < len)
		{
			acc += iPtr[i];
		}
		return acc;
	}

	public static int CheckItSpan(int* iPtr, int len)
	{
		Span<int> span = .(iPtr, len);

		int acc = 0;
		for (int i < len)
		{
			acc += span[i];
		}
		return acc;
	}

	public static int CheckItSpanOpt(int* iPtr, int len)
	{
		OptSpan<int> span = .(iPtr, len);

		int acc = 0;
		for (int i < len)
		{
			acc += span[i];
		}
		return acc;
	}
}

struct Blurg
{
	static int GetHash<T>(T val) where T : IHashable
	{
		return val.GetHashCode();
	}

	public static int32 LongCall(
		int abcdefghijklmnopqrstuvwxyz0,
		int abcdefghijklmnopqrstuvwxyz1,
		int abcdefghijklmnopqrstuvwxyz2,
		int abcdefghijklmnopqrstuvwxyz3,
		int abcdefghijklmnopqrstuvwxyz4,
		int abcdefghijklmnopqrstuvwxyz5,
		int abcdefghijklmnopqrstuvwxyz6,
		int abcdefghijklmnopqrstuvwxyz7,
		int abcdefghijklmnopqrstuvwxyz8,
		int abcdefghijklmnopqrstuvwxyz9
		)
	{
		return 0;
	}

	static mixin ScopedAlloc(int size, int align)
	{
		//(void*)scope:mixin [Align(align)] uint8[size]* { ? }
	}

	public static void TestAlloc()
	{
		int i = 1;
		if (i == 1)
		{
			int size = 128;
			scope:: int[size]*;
		}
	}

	struct StructA
	{
		public int[10] mA;

		public this()
		{
			mA = default;
			void* v = &this;
		}
	}

	enum EnumA
	{
		case None;
		case A(StructA sa);
	}

	enum EnumB
	{
		case A;
		case B(int a, int b);
	}

	/*[DisableChecks]
	public static float GetSum<TCount>(float[TCount] vals) where TCount : const int
	{
		float total = 0;
		for (int i < vals.Count)
			total += vals[i];
		return total;
	}

	public static void Max<T, TFunc>(T lhs, T rhs, TFunc func) where TFunc : delegate int(T lhs, T rhs)
	{

	}*/

	public struct Base
	{
		int32 mA;
		int64 mB;
	}

	public struct Derived : Base
	{
		int8 mC;

		public int GetC()
		{
			return mC + 10000;
		}
	}

	static int[] gArr = new .(1, 2, 3, 4, 5, );

	[Checked]
	public static int32 GetVal()
	{
		return 1;
	}

	[Unchecked]
	public static int32 GetVal()
	{
		return 2;
	}

	public static int32 GetVal2()
	{
		return 3;
	}

	
	public static void Test()
	{
		//Test_Start
		Derived dr = .();
		dr.GetC();
		Int iVal = (.)123;

		int q = 999;

		//Test_End
	}

	public static void Test2(int aa, int bb, int cc)
	{
		//Test_Start
		Derived dr2 = .();
		Int iVal2 = (.)123;

		int q2 = 999;

		String str = scope .();

		//Test_End
	}

	public static void Recurse(int a)
	{
		int b = 234;
		//Recurse_C
		int c = 345;

		if (a == 10)
			return;

		Recurse(a + 1);
		int d = 100 + a;
	}

	public static void Test3()
	{
		//BreakpointTester_Test
		int a = 0;
		int b = 0;

		while (a < 20)
		{
			//BreakpointTester_LoopA
			a++;
		}

		//BreakpointTester_Recurse
		Recurse(0);
	}

	public static void Test4()
	{
		//Test_Start
		Derived dr = .();
		Int iVal = (.)123;

		int q = 999;

		//Test_End
	}

	//[DisableObjectAccessChecks]
	public static void Hey2()
	{
		String str = "Hey";
		    //int len = str.[Friend, DisableObjectAccessChecks]PrivateLength;
		int len = str.[DisableObjectAccessChecks]Length;
		 //int len = str.[Friend]GetLength();
	}				   

	public static int32 Hey()
	{
		Hey2();
		Test();
		Test2(11, 22, 33);
		Test3();
		Test4();
		NoFrame.Test();
		return (int32)123;
	}

}


class NoFrame
{
	public static void Test()
	{
		//Test_Start
		Blurg.Derived dr = .();
		Int iVal = (.)123;

		int q = 999;

		//Test_End
	}
}