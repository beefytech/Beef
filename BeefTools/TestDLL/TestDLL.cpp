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


struct [[nodiscard]] StructA
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

[[nodiscard]]
int GetVal()
{
	return 9;
}

StructA GetSA()
{
	return StructA();
}

// THIS IS VERSION 3.
extern "C"
__declspec(dllexport) void Test2(int aa, int bb, int cc, int dd)
{	
	GetVal();
	GetSA();

	//Sleep(10000);

	StructA sa;
	sa.mSB = NULL;
	Sleep(200);
	sa.GetVal();
	sa.GetWithSleep();	
	//auto val = sa.mSB->mStr;

	std::string str = "Hey Dude";
	str.push_back((char)0x85);
	std::wstring str2 = L"Hey Dude";
	str2.push_back((wchar_t)0x85);
	str2.push_back((wchar_t)0x263a);

	int a = 123;
	int b = 234;
	int c = 345;

	//GetA();
	//GetB();
}

extern "C"
__declspec(dllexport) void Test3(int a, int b)
{
	
 	
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