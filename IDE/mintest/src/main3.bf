#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;

struct Zoops
{
	public int mA = 123;
	public int mB = 234;

	public static implicit operator Zoops(float f)
	{
		Zoops val = default;
		val.mA = (.)f;
		val.mB = 200;
		return val;
	}

	public static implicit operator Zoops(float[2] f)
	{
		Zoops val = default;
		val.mA = (.)f[0];
		val.mB = (.)f[1];
		return val;
	}
}

struct Blurg
{
	public void Foo<T>()
	{

		
	}

	public void Zarf<T>()
	{
		T Yorp<T2>(T2 val)
		{
			return default;
		}
	}

	[CallingConvention(.Cdecl)]
	public static void Hey()
	{
		
	}

}

struct StructA
{
	public int mA = 99;
}



/*namespace System
{
	extension String
	{
		public String SubText(String input, int position, int length)
		{
			return 0;
		}
	}
}*/