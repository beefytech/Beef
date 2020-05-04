#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;

[Obsolete("This is old", false)]
class Bloops
{

}

struct Blurg
{
	[LinkName(.Empty)]
	public static void Hello()
	{

	}

	[CallingConvention(.Cdecl)]
	public static void Hey()
	{
		Hello();

		//int a = LinkNameAttribute(.);
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