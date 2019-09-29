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

class Norg
{
	public String mVal;
	public int32 mA;
	public int32 mB;

	public int32 GetIt(int32 a, int32 b, int32 c) mut
	{
		return a + b + c + mA;
	}

	public static int32 GetIt(Blurg bl, int32 a, int32 b, int32 c)
	{
		return a + b + c + bl.mA;
	}
}

struct Blurg 
{
	public String mVal;
	public int32 mA;
	public int32 mB;

	public this()
	{
		mVal = "z";
		mA = 111;
		mB = 222;
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
		PrintF("a0");
	}

	static void Test(int a, int b, int c)
	{

	}

	
	public static int32 Hey()
	{
		Result<int, float> res = .Ok(123);

		Florg fl = .();

		let f2 = fl;
		//defer f2.Dispose();

		using (var f = fl)
		{
			
		}
		
		return 123;
	}

}

struct Florg
{
	int mA = 123;

	public void Dispose() mut
	{

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

