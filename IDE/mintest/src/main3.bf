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

class Bloozer
{
	int mA;
}

enum Zorf : IDisposable
{
	case A;
	case B;

	public void Dispose()
	{

	}
}

class IFaceA
{
	public static void Fart()
	{

	}
}

class Zlips : IFaceA, IDisposable
{
	static void Fart()
	{

	}

	public void Dispose()
	{
		PrintF("Disposed");
	}
}

class Testo
{
	public this()
	{
		PrintF("Testo this %p\n", this);
	}

	public ~this()
	{
		PrintF("Testo ~this %p\n", this);
	}
}

class Blurg
{
	delegate void() mFuncA;
	delegate void() mFuncB;

	int mA = 123;

	public this()
	{
		
	}

	void TestIt(String a, String b)
	{

	}

	TestStruct GetTS()
	{
		return .();
	}

	static void Test(int a, int b)
	{

	}

	static void Test(int a, int b, int c)
	{

	}

	public static void Use<T>(T val) where T : IFaceA
	{
		IFaceA.Fart();
	}

	public static void Hey()
	{
		Loop:
		for (int i < 10)
		{
			defer
			{
				//for ()
				JLoop: for (int j < 5)
				{
					//continue Loop;
				}

				//break JLoop;

				int z = 3;

				/*void Zorg()
				{
					
				}

				Zorg();*/
				//return;

				//break Loop;
			}
		}

		defer
		{
			scope:: Testo();

			int i = 0;
			if (i == 0)
				scope:: Testo();
			if (i == 1)
				scope:: Testo();
		}

		

		int aaaaaa = 123;
		if (aaaaaa == 123)
			return;//A

		int bbbbbbb = 222;
		return;//B
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

