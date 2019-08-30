//GORB
#pragma warning disable 168

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

//#define A
//#define B

class ClassA
{
	public virtual void ClassA0()
	{

	}

	public virtual void ClassA1()
	{

	}
}

class ClassB
{
	
}

#if B
class ClassC : ClassB
{
	public override void ToString(System.String strBuffer)
	{
		base.ToString(strBuffer);
	}
}
#elif A
class ClassC : ClassA
{
	public override void ToString(System.String strBuffer)
	{
		base.ToString(strBuffer);
	}
}
#endif

class ClassD
{
	public String mStr;
	int mA6;

	public virtual void Poo()
	{
		PrintF("Poo\n");
	}

	public virtual void Poo2()
	{
		PrintF("Poo2\n");
	}
}

class ClassD2
{
	int mA5;

}

class ClassE : ClassD
{
	public void Zog2()
	{

	}
}

class ClassF : ClassE
{

}

[NoDiscard("Use this value!")]
struct TestStruct
{
	public int mA;
	public int mB;
}

class Blurg
{
	[Export, CLink, StdCall]
	public static void Poof()
	{
		PrintF("Poofs!\n");
	}

	[Export, CLink, StdCall]
	public static void Poof2()
	{
		PrintF("Poofs2!\n");
	}

	static void Test0()
	{
		Snorf sn = scope .();
		int a = 124;
	}


	public void DoRecurse(int depth, ref int val)
	{
		Thread.Sleep(1);
		++val;
		if (val < 5)
		{
			DoRecurse(depth + 1, ref val);
		}
	}

	public static void VoidCall()
	{
		
	}

	public static int GetInt()
	{
		return 123;
	}

	public static void Hey()
	{
		TestStruct ts = .();
		//int val = ts..mA;
		ts.mA = 123;

		VoidCall();
		int val0 = GetInt();

		Blurg bl = scope .();
		int val = 0;
		bl.DoRecurse(0, ref val);

		Test0();
		Test0();
		Test0();
		Test0();

		Result<void*> voidPtrResult = default;

		//void* val = voidPtrResult;


		//Result<TestStruct> ts = .Ok(.());

		//let val = ts.Get();
		//Snorf.Bloog bl = .();

		//Poof();
	}

}

class CustomAlloc
{
	public void* Alloc(int size, int align)
	{
		//return Internal.Malloc(size);
		return null;
	}
}