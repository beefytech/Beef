//using Squarf;

//GORB
#pragma warning disable 168

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

#region EnumA Crapsos
[AllowDuplicates]
enum EnumA
{
	Abo = 1,
	Boop = _*2,
	Croop = _*2,

	Zoop = 1
}

struct StructA
{
	public int mA;
	public int mB;
}

#region Blurg
struct Blurg
{
	static mixin MixA(var sa2)
	{
		sa2.mA++;
	}

	static mixin MixB(mut StructA sa2)
	{
		sa2.mA++;
	}

	static void MethodA(mut StructA sa)
	{
		MixA!(sa);
		//MixB!(mut sa);
	}

	public static int32 Hey()
	{
		StructA sa = .();
		sa.mA = 123
		
		return (int32)123;
	}
}

