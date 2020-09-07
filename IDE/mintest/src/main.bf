
//♀Farts 
//Ãjaxa̐ḁ

//#if false

#pragma warning disable 168

//zab1234

// Zoop
//using IDE;
using System;
//using System.Threading;
using System.Collections;
using System.Diagnostics;
using System.Collections;
using System.Collections;
using System.Threading;

// Disable unused variable warning
#pragma warning disable 168

public enum QIntDisplayType
{
    Default,
    Decimal,
    Hexadecimal,
    Binary,
    COUNT																									  
}

[CRepr]
public struct ALLEGRO_COLOR
{
    public float r, g, b, a;
}

namespace Hey.Dude.Bro
{
	class TestClass
	{
		static int gApzong = 123;

		[CLink, CallingConvention(.Stdcall)]
		public static extern void OutputDebugStringA(char8* str);

		/*[CLink, CallingConvention(.Stdcall)]
		internal static extern void OutputDebugStringW(char16* str);*/

		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetTickCount();

		[Import("winmm.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 timeGetTime();

		/*public const String cDllName = @"C:\proj\TestDLL2\x64\Debug\TestDLL2.dll";
		[DllImport(cDllName), CLink, CallingConvention(.Stdcall)]
		public static extern void Test2(int a, int b, int c, int d);*/

		//[DllImport(cDllName), CLink, CallingConvention(.Stdcall)] public static extern void Test3();

		//[DllImport(@"C:\Beef\IDE\dist\IDEHelper64_d.dll"), CLink, CallingConvention(.Stdcall)]
		//public static extern void BFTest();

		/*[CallingConvention(.Stdcall), CLink]
		public static extern void ExTest2();*/

		//[DllImport(@"\beef\ide\dist\TestDLL.dll"), CLink]
		//[DllImport(@"C:\Beef\BeefTools\TestDLL\x64\Debug\TestDLL.dll"), CLink]

#if BF_64_BIT
		[Import(@"C:\Beef\BeefTools\TestDLL\x64\Debug\TestDLL.dll"), LinkName("Test2")]
		public static extern void Test2(int32 a, int32 b, int32 c, int32 d);

		[Import(@"C:\Beef\BeefTools\TestDLL\x64\Debug\TestDLL.dll"), LinkName("Test3")]
		public static extern void Test3(void* ptr, ALLEGRO_COLOR color);
#else
		[Import(@"C:\Beef\BeefTools\TestDLL\Debug\TestDLL.dll"), LinkName("Test2")]
		public static extern void Test2(int32 a, int32 b, int32 c, int32 d);

		[Import(@"C:\Beef\BeefTools\TestDLL\Debug\TestDLL.dll"), LinkName("Test3")]
		public static extern void Test3(void* ptr, ALLEGRO_COLOR color);

		[Import(@"C:\Beef\BeefTools\TestDLL\Debug\TestDLL.dll"), LinkName("Test4")]
		public static extern void Test4(void* ptr, ALLEGRO_COLOR* color);
#endif

		

		
		static uint32 sStaticVar = 234;

		public static int FartsieInt<T>(T val) where T : const int
		{
			 return T;
		}

		public static void UseTC(QIntDisplayType tc)
		{

		}

		public static int Fartsie<T>(TypeCode tc, T val = .Boolean) where T : const TypeCode
		{
			TypeCode tc2 = val;

			UseTC((QIntDisplayType)val | .Binary);
			//return (int)T;
			return 99;
		}

		public static void MethodA(int a, TypeCode b = .Char16)
		{

		}

		public static void MethodA<T>(int a, TypeCode b, T c) where T : const TypeCode
		{

		}

		static int Test()
		{
			//for (int i < 20)
			for (int i < 50)
			{
				//OutputDebugStringA("Test......................................\r\n");
				Thread.Sleep(100);
			}

			return 123;
		}
 
		static int64 foo(int32 *x, int64 *y)
		{
		  *x = 0;
		  *y = 1;
		  return *x;
		}

		enum EnumA
		{
			case Abc;
			case Def;

			public static EnumA Gorf()
			{
				return Abc;
			}

			public static EnumA Gorzom()
			{
				return .Def;
			}

			public static void Goo()
			{

			}
		}

		enum EnumB
		{
			AAA,
			BBB
		}

		enum EnumC
		{
			CCC,
			DDD
		}

		static void Flarg()
		{
			EnumA ea;
			Test();
		}

		static int RunLong()
		{
			Thread.Sleep(5000);
			return 999;
		}

		static void Thread()
		{
			int a = 123;
			PrintF("A...\n");
			Thread.Sleep(1000);
			PrintF("B...\n");
			Thread.Sleep(1000);
			PrintF("C...\n");
			Thread.Sleep(1000);
			PrintF("D...\n");

			int abc = 234;
		}

		static int GetVal()
		{
			return 111;
		}

		public static void TestA()
		{
			int* a = null;
			*a = 123;
		}

		public static int32 SEHHandler(void* ptr)
		{
			PrintF("SEH Handler at root\n");
			//Thread.Sleep(15000);
			return 0;
		}

		[CRepr, CLink]
		public static extern void* SetUnhandledExceptionFilter(function int32(void* p) func);

		public static int GetHash<T>(T val) where T : IHashable
		{
			return val.GetHashCode();
		}	

		class Zangles
		{
			public static int GetMe(Zangles zang, String str)
			{
				return 99;
			}
		}

		public static void Florgs()
		{

		}

		public static int Main(String[] args)
		{
			Blurg.Hey();
			return 1;
		}

		public static this()
		{
			//Runtime.TestCrash();
		}
	}
}

public struct Color : uint32
{

}

struct Florf
{
	public int Zorg()
	{
		return 42;
	}

	public int Goof()
	{
		return Zorg();
	}


}


struct StructAz
{
	public struct StructB
	{

		public struct StructC
		{
			public static void MethodC0()
			{
				MethodA0();
			}
		}

		public static void MethodB0()
		{
		}
	}

	public static void MethodA0()
	{
	}

}

namespace IDE
{
	static
	{
		 public static Object gApp = null;
	}

	static
	{
		 public static Object gApp2 = null;
	}
}

namespace AA
{
	namespace BB
	{
		namespace CC
		{
			static
			{
				public static int gValue = 123;
				public static String gStr = "Hey";
			}
		}
	}
}

static
{
	static int gApsings = 123;
}

/*namespace zSquarf
{
	class Zorf
	{

	}
}*/
