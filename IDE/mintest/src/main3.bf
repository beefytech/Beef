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

	public static int32 Hey()
	{
		TypeCode tc = .Boolean;
		//int h = GetHash(tc);

		var val = tc.Underlying;
		var valRef = ref tc.UnderlyingRef;

		let maxVal = typeof(TypeCode).MaxValue;

		int a = 100;

		String str = new:gCRTAlloc String(a);

		delete:gCRTAlloc str;


		return 123;
	}

}
