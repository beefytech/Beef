using System;

namespace LibB
{
	class LibB0
	{
		public static int GetOverload0<T>() where T : var
		{
			T val = default;
			return Overload0(val);
		}
	}
}

extension LibClassA
{
	public int32 mB = GetVal(8, "LibB.LibClassA.mB");

	public this()
	{
		PrintF("LibB.LibClassA()\n");
		mB += 100;
	}

	public this(int32 a)
	{
		PrintF("LibB.LibClassA(int32)\n");
		mB += 1000;
	}

	public int32 LibB_GetB()
	{
		return mB;
	}
}

static
{
	public static int Overload0(int16 a)
	{
		return 2;
	}
}

namespace LibSpace
{
	static
	{
		public static int MethodB()
		{
			return 200;
		}
	}
}