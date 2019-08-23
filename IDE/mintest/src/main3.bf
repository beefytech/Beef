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

class Snorf
{
	public static void Borf(ClassD cd)
	{
		cd.Poo();
	}
}

[NoDiscard("Use this value!")]
struct TestStruct
{
	public int mA;
	public int mB;
}

class Blurg
{
	public static void UseTS(TestStruct ts)
	{
		int c = ts.mA;
	}

	[NoDiscard("Why you no use me?")]
	public static int UseMe()
	{
		return 999;
	}

	public static TestStruct UseIt()
	{
		return .();
	}

	mixin FartOut(int a)
	{
		a = 0;

		//let c = 1;
		//c = 3;
		//int b = 123;
		//int c = 99;
	}

	public static void Hey()
	{
		mixin Fart(int a)
		{
			//a = 0;
			//int b = 123;
			//int c = 99;
		}

		/*int c = 222;
		Fart!(c);*/

		//int __CRASH_AUTOVARIABLE__ = 123;

		//UseIt();

		//UseMe();

		//int a;


		for (int i < 10)
		{
			int a = 0;
		}

		TestStruct ts = .();
		ts.mA = 111;
		ts.mB = 222;
		UseTS(ts);

		ClassD cd = null;
		//let str = cd.mStr;


#if A || B
		ClassC cc = scope .();
#endif
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