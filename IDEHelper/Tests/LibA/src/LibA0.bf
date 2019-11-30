using System;
namespace LibA
{
	class LibA0
	{
	}
}

class LibClassA
{
	public int32 mA = GetVal(7, "LibA.LibClassA.mA");

	public this()
	{
		PrintF("LibA.LibClassA()\n");
		mA += 100;
	}

	public this(int32 a)
	{
		mA += a;
	}

	public static int32 GetVal(int32 val, String str)
	{
		PrintF("GetVal: %s\n", str.CStr());
		return val;
	}

	public int GetVal2()
	{
		return 9;
	}

	public static int GetVal3(Object obj)
	{
		return 30;
	}
}

class LibClassB
{
	public static int DoGetVal3<T>(T val)
	{
		return LibClassA.GetVal3(val);
	}
}