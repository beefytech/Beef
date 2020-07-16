namespace LibC
{
	class LibC0
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
	public int32 mB = GetVal(13, "LibC.LibClassA.mB");

	public this(int8 i8)
	{
		PrintF("LibC.LibClassA()\n");
		mB += 30000;
	}

	public int LibC_GetB()
	{
		return mB;
	}
}

static
{
	public static int Overload0(int32 a)
	{
		return 3;
	}
}

namespace LibSpace
{
	static
	{
		public static int MethodA()
		{
			return 100;
		}
	}
}