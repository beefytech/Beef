//using Squarf;

//GORB
#pragma warning disable 168

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

[AllowDuplicates]
enum EnumA
{
	Abo = 1,
	Boop = _*2,
	Croop = _*2,

	Zoop = 1
}

struct Blurg
{
	public static int32 Hey()
	{
		int a = 123;
		int* aPtr = &a;
		
		return (int32)123;
	}
}

