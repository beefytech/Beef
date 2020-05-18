using System;
namespace LibA
{
	interface IVal
	{
		int Val
		{
			get; set;
		}
	}

	class LibA0
	{
		public static int GetVal<T>(T val) where T : IVal
		{
			return val.Val;
		}

		public static void Dispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		public static void Alloc<T>() where T : new, delete
		{
			let t = new T();
			delete t;
		}
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