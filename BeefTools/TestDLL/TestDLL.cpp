#define _ENABLE_ATOMIC_ALIGNMENT_FIX

#include <windows.h>
#include <stdlib.h>
#include <cstdio>
#include "TestDLL.h"
#include <map>
#include <vector>
#include <atomic>
#include <functional>

namespace Beefy
{
	template <typename TKey, typename TValue>
	class Dictionary
	{
	public:
		struct Entry
		{
			TKey mKey;
			TValue mValue;;
		};
	};
}

template <typename T>
struct FliffT
{
	T mVal;
};

bool CheckIt()
{
	return true;
}

int GetA()
{
	return 123;
}

int GetB()
{
	for (int i = 0; i < 10; i++)
	{
		Sleep(100);
		printf("Hey %d\n", i);
	}
	return 234;
}

struct StructB
{
	std::string mStr;
};


struct StructA
{
	StructB* mSB;

	int GetVal()
	{
		return 123;
	}

	int GetWithSleep()
	{
		Sleep(5000);
		return 234;
	}
};

int GetVal()
{
	return 9;
}

StructA GetSA()
{
	return StructA();
}

int Zorq()
{
	int zaf = 123;
	return 0;
}

struct Base
{
	int32_t mA;
	int64_t mB;
};

struct Derived : Base
{
	int8_t mC;

	int GetC()
	{
		return mC + 10000;
	}
};

struct Int
{
	int64_t mVal;
};

void Zorq2()
{
	Derived dr;
	dr.mA = 1;
	dr.mB = 2;
	dr.mC = 3;
	dr.GetC();
	Int iVal;
	iVal.mVal = 88;

	int64_t q = 999;
}

void Zorq3()
{
	Derived dr;
	dr.mA = 1;
	dr.mB = 2;
	dr.mC = 3;	
	Int iVal;
	iVal.mVal = 88;

	int64_t q = 999;
}

void TestMem()
{
	char* mem = (char*)::VirtualAlloc(0, 4096 * 2, MEM_RESERVE, PAGE_READWRITE);
	::VirtualAlloc(mem, 4096, MEM_COMMIT, PAGE_READWRITE);

	char* str = "Hey";
	char* cPtr = mem + 4096 - 3;
	memcpy(cPtr, str, 3);
}

void Test6()
{

}

void Test5(int a, void* b, void* c)
{

}

void Test4(int a, int b, int c)
{
	Test5(10, Test6, NULL);
}

void Test3(int a)
{
	Test4(100, 200, 300);
}

// THIS IS VERSION 6.
extern "C"
__declspec(dllexport) void Test2(int aa, int bb, int cc, int dd)
{	
	Test3(10);

	char* strP = "Hey yo";

	TestMem();

	Zorq();
	Zorq2();
	Zorq3();

	GetVal();
	GetSA();

	//Sleep(10000);

	int zed = 999;

	StructA sa;
	sa.mSB = NULL;
	Sleep(200);
	//sa.GetVal();
	//sa.GetWithSleep();	
	//auto val = sa.mSB->mStr;

	std::string str = "Hey Dude";
	str.push_back((char)0x85);
	std::wstring str2 = L"Hey Dude";
	str2.push_back((wchar_t)0x85);
	str2.push_back((wchar_t)0x263a);

	int a = 123;
	int b = 234;
	int c = 345;
}

struct ALLEGRO_COLOR
{
	float r, g, b, a;
};

extern "C"
__declspec(dllexport) void Test4(void* ptr, ALLEGRO_COLOR* colorPtr)
{
	printf("Color: %f %f %f %f\n", colorPtr->r, colorPtr->g, colorPtr->b, colorPtr->a);
}

extern "C"
__declspec(dllexport) void Test3(void* ptr, ALLEGRO_COLOR color)
{
	printf("Color: %f %f %f %f\n", color.r, color.g, color.b, color.a);
 
	Test4(ptr, &color);
	//printf("Hey!\n");


}

static long __stdcall SEHFilter(LPEXCEPTION_POINTERS lpExceptPtr)
{
	printf("SEHFilter!\n");
	return 0;
}

extern "C"
__declspec(dllexport) void TestSEH()
{
	if (::MessageBoxA(NULL, "DO IT?", "Crash?", MB_YESNO) == IDNO)
		return;

	::SetUnhandledExceptionFilter(SEHFilter);

	int* iPtr = nullptr;
	*iPtr = 1;
	//printf("Hey!\n");
}