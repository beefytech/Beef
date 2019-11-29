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

	struct Base
	{
		int32 mA;
		int64 mB;
	}

	struct Derived : Base
	{
		int8 mC;
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

	public static int32 Hey()
	{
		/*Self.[Checked]GetVal();
		Self.[Unchecked]GetVal();
		GetVal2();*/

		int a = gArr[1];
		a = gArr[[Unchecked]2];

		return (int32)123;
	}

}

