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
}

class LibClassB
{
	//public uint8* mA = append uint8[10];
}