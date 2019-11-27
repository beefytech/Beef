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

	public static int32 Hey()
	{
		//int_test val = 123;

		(int, int) tup = (1, 3);

		switch (tup)
		{
		case (1, var ref a):
			a++;
			PrintF("A\n");
		default:
			PrintF("B\n");
		}

		
		if (tup case (1, var ref aa))
		{
			aa += 100;
		}

		/*EnumB eb = .B(1, 2);

		if (eb case .B(1, var ref bb))
		{

		}*/
		
		

		return 123;
	}

}
