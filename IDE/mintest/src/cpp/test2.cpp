#include <stdio.h>
#include <list>
  
#define BF_EXPORT extern "C" __declspec(dllexport)
#define BF_CALLTYPE __stdcall

int gValThing = 123;

template <typename T>
void TestFunc(T func)
{
	for (int i = 0; i < 100000000; i++)
	{
		func();
	}
}

template <typename T>
T Zoofgers(T val)
{
	return val;
}

class ClassAz
{
	virtual void CallAz()
	{
		printf("ClassA::CallA\n");
	}

	virtual void CallBz()
	{
		printf("ClassA::CallB\n");
	}

	virtual void CallCz(int a)
	{
		printf("ClassA::CallC\n");
	}
};

class ClassBz : public ClassAz
{
	virtual void CallBz() override
	{
		printf("ClassB::CallB\n");
	}

	virtual void CallCz(int a) override
	{
		printf("ClassB::CallC\n");
	}
};

class ClassCz : public ClassBz
{
	virtual void CallCz(int a) override
	{
		printf("ClassC::CallC\n");
	}

	void Poopsie(int a)
	{

	}

	static void Poopsie2(int a)
	{

	}
};

/*int64_t TestLoop(int64_t a, int64_t b)
{
	int64_t res = 100;
	for (int64_t i = 0; i < a; i++)
	{
		res += b;
	}
	return res;
}*/

struct SmallStruct
{
	int mA;
	int mB;
};

void TestSmallStruct(SmallStruct ss, int s2)
{

}

enum FartTest
{
	Fart0,
	Fart1
};

//enum FartForward;

extern "C" char str0[] = {'A', 'B', 'C', 'D', 'E', 'F', 0 };
extern "C" char str1[] = {'a', 'b', 'c', 'd', 'e', 'f', 0 };

template <typename T>
void Poofie(int a)
{
	printf("1\n");
}

void Fart();

BF_EXPORT void BF_CALLTYPE ExTest2()
{
	Poofie<int>(123);
	Fart();

	wchar_t wc = L'1';

	/*asm(
		".intel_syntax;"
        "mov rax, 123;"
		"mov rcx, 3;"
		"lea rsi, str0;"
		"lea rdi, str1;"
		"rep movsb;"

		: "=*mr" (str0)
		: "r" (str1)
        );*/

	FartTest fs;

	SmallStruct ss;
	ss.mA = 1;
	ss.mB = 2;
	TestSmallStruct(ss, 123);

	SmallStruct* ssPtr = &ss;

	SmallStruct& ssRef = ss;

	SmallStruct*& ssPtrRef = ssPtr ;

	//auto result = TestLoop(3, 4);///////
	ClassCz* classCz = new ClassCz();
	ClassBz* classBz = classCz;
	ClassAz* classAz = classCz;
	//9
	std::list<int> intList;
	intList.push_back(22);
	intList.push_back(33);
	intList.push_back(44);

	int i = 209;
	auto act = [&]()
	{
		i++;
	};

	Zoofgers(1);
	Zoofgers(1.2f);

	TestFunc(act);

	/*auto actPtr = &act;
	for (int i = 0; i < 100000000; i++)
		(*actPtr)();*/
	printf("Result: %d\n", i);
}
