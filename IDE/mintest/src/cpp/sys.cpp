// This is a test class for the debugger.  This file isn't actually used or required.
// zzzqzzzwqeqqew
  
//#include "BFPlatform.h"
//#include "xmmintrin.h"

#ifdef INCLUDE_SYS//////
//#include "sys.h"
#endif

#include <Windows.h> 
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <math.h>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <stdint.h>
#include <xmmintrin.h>
//#include <x86intrin.h>
//#include <corecrt_io.h>
//#include <alloca.h>

extern "C" int getpid();

#include <assert.h>

#ifdef NOT_DEFINED
    std::string gStr = "Hey, this isn't really used!";
#endif

//extern int sStaticVal2;

class Fartie
{
public:
	static void FartTest(int a, int b)
	{
		printf("Hey: %d %d\n", a, b);
	}
};

extern "C" void FartTest()
{
	Fartie::FartTest(2, 3); 
}

int sStaticVal3 = 99;

//__declspec(dllimport) int gDLLVariable;
//__declspec(dllimport) extern "C" void DLLMethod();

__declspec(dllexport)
extern "C" void FloatTest(float val)
{
	Fartie::FartTest(1, 2); 
}

void FloatTest2(float val)
{

}

int CPPGetValue(float fVal)
{
	//DLLMethod();
	//gDLLVariable = 9;
	
	return (int)fVal;
}

int GetValue2(float fVal, double dVal)
{
    return 123;
}

int GetValue2(int fVal, int dVal)
{
    return 123;
}

int GetValue2(__int64 i64Val, int iVal = 234)
{
    return 123;
}

int& GetIntRefResult()
{
	static int iVal = 999;
	return iVal;
}

namespace System
{
    struct Int32
    {
    public:
    	int mValue;
	};
}

//
namespace MultiClass
{
	class Class0
	{
	public:
		int mVal01;
		int mVal02;

	public:
		Class0(int a1, int a2)
		{
			mVal01 = a1;
			mVal02 = a2;
		}

		virtual void VCall0()
		{
			printf("Class0.VCall0 %d", mVal01);
		}

		virtual void VCall1()
		{
			printf("Class0.VCall1 %d", mVal01);
		}
	};

	class ClassA : public virtual Class0
	{
	public:
		int mValA1;
		int mValA2;

		ClassA(int a1, int a2) : Class0(13, 14)
		{
			mValA1 = a1;
			mValA2 = a2;
		}
	};

	class ClassB : public virtual ClassA
	{
	public:
		int mValB1;
		int mValB2;

		ClassB() : ClassA(1, 2), Class0(17, 18)
		{
			mValB1 = 3;
			mValB2 = 4;
		}
	};

	class ClassC : public virtual ClassA
	{
	public:
		int mValC1;
		int mValC2;

		ClassC() : ClassA(5, 6), Class0(19, 20)
		{
			mValC1 = 7;
			mValC2 = 8;
		}

		virtual void VCall2()
		{
			printf("ClassC.VCall2 %d", mValC1);
		}
	};

	class ClassD : public ClassB, public ClassC
	{
	public:
		int mValD1;
		int mValD2;

		ClassD() : ClassA(11, 12), Class0(15, 16)
		{
			mValD1 = 9;
			mValD2 = 10;
		}

		virtual void VCall1()
		{
			printf("ClassD.VCall1 %d", mValC1);
		}

		virtual void VCall2()
		{
			printf("ClassD.VCall2 %d", mValC1);
		}
	};
}

/*template <typename T>
struct GenericTest
{
    void Check(float iVal)
    {

	}

    void Check(T iVal)
    {

	}
};

GenericTest<float> gGenericTest;

void Test()
{
    //gGenericTest.Check(1);
    gGenericTest.Check(1.0f);
}*/

__thread int gStaticVal0;
int& gStaticValRef0 = gStaticVal0;

__attribute__((always_inline))
int CTest3(int iVal3)
{
    iVal3++;
    if (iVal3 == 199)
        return 4;
    return 5;
}

__attribute__((always_inline))
int CTest2(int iVal)
{
    int iVal2 = iVal + CTest3(iVal);
    if (iVal2 == 99)
        return 343;
    iVal2++;
    return iVal2;
}

void IRef(int& i)
{
    i++;
}

struct CPPStruct
{
public:
	int mVal;

public:
	int CPPS_GetVal(CPPStruct& structRef, CPPStruct* structPtr)
	{
		int a = 99 + structRef.mVal + structPtr->mVal;
		return mVal + 99 + structRef.mVal;
	}

	int CPPS_GetValToo(CPPStruct structVal)
	{
		int a = 99 + structVal.mVal;
		return mVal + 99;
	}
};

class CPPClass
{
public:
	int mVal;

public:
	int HeyBro()
	{
		return mVal;
	}

	static const int cVal1 = 99;

	enum
	{
		INNER_ENUM = 88
	};
};

#pragma pack(1)
class Class00
{
public:
	/*union
	{        
		float mVal1;
		int mVal1b;
		double mVal1c;
	};*/
	char mVal0;
	short mVal1;
	int mVal2;
	int mVal3;
};
#pragma pack(0)

namespace Hey
{
	namespace Dude
	{
		namespace Bro
		{
            extern int sStaticVal;

		    struct Vector2
		    {
                //static const char* cCharPtr = "Yo!";

		        float mX;
		        float mY;
		
                Vector2()
                {

				}

                Vector2(float x, float y)
                {

				}

		        void Write();
			};

            struct TestClass
            {
                static void CPPSTest(int iParam);
			};

			struct StructC1
			{
				int mValA;
				int mValB;

				StructC1()
				{

				}

				StructC1(int a, int b)
				{
					mValA = a;
					mValB = b;
				}
			};

			void MethodA()
			{
				Class00 class00;
				class00.mVal1 = 1;

				StructC1 c1Arr[4] = { StructC1(10, 20), StructC1(20, 30), StructC1(30, 40), StructC1(40, 50) };

				std::vector<StructC1> c1Vec = { StructC1(10, 20), StructC1(20, 30), StructC1(30, 40), StructC1(40, 50) };

				std::map<int, StructC1> mapA;				
				mapA[2] = StructC1(1, 2);
				mapA[1] = StructC1(2, 3);
				mapA[3] = StructC1(3, 4);
				mapA[0] = StructC1(4, 5);

				auto& firstVal = *mapA.begin();
				int z = 99;
			}

			void MethodB()
			{
				StructC1 c1Arr[3] = { StructC1(1, 2), StructC1(2, 3), StructC1(3, 4) };

				std::vector<StructC1> c1Vec = { StructC1(1, 2), StructC1(2, 3), StructC1(3, 4) };

				std::map<int, StructC1> mapA;				
				mapA[20] = StructC1(10, 20);
				mapA[10] = StructC1(20, 30);
				mapA[30] = StructC1(30, 40);
				mapA[00] = StructC1(40, 50);
				
				for (auto val : c1Vec)
				{
					val.mValA = 2;
				}
			}

			int MethodZ0(int val)
			{
				return 1;
			}

			int MethodZ1(int val)
			{
				return 1;
			}

			void TestStruct(StructC1 sc)
			{
				StructC1 sC2;
				sC2.mValA = 99;
				sC2.mValB = 88;

				sc.mValA++;
				sc.mValB++;				
			}

			void Yo(std::vector<int> intVectorA, std::vector<int> intVectorB)
            {
				int z = 9;
            }



            //1
            //2
            //3
            //4
            void TestClass::CPPSTest(int iParam)
            {
				std::vector<int> intVector;
				Yo(intVector, intVector);

				std::string str = "Hey";
				std::string& str2 = str;

				int iVal = 123;
				int& iVal2 = iVal;

				StructC1 sC1;
				sC1.mValA = 22;
				sC1.mValB = 33;

				/*for (int i = 0; i < 100000; i++)
				{
					OutputDebugStringA("This is a test of the OutputDebugString system\n");
				}*/

				//int pid = getpid();

				TestStruct(sC1);

				//assert(false);

				::MessageBoxA(NULL, "Test", "Zap", 1);

				const char* msg = "Hey!";

				MethodZ0(MethodZ1(123));

				{
					int zzz = 123;
					printf("zzz: %d\n", zzz);
				}
				int qqq = 333;

				for (int i = 0; i < 10; i++)
				{
					MethodA();
					MethodB();
					printf("Hey %d\n", i);
				}				

				MultiClass::ClassD classD;
				MultiClass::ClassB* classB = &classD;
				MultiClass::ClassC* classC = &classD;
				MultiClass::ClassA* classA = classC;

				try
				{
					throw classB;
					printf("After throw\n");
				}
				catch (void* ex)
				{
					printf("Inside handler\n");
				}

				printf("Outside handler\n");

				static int sCPPSTestLocalVar = 9;

				{
                    static const int iCPPConstValue = 123;
				}

				const void* voidPtr = &iParam;

				int j = sCPPSTestLocalVar;
				j = CPPGetValue(3.4f);
				j = sStaticVal3;

				CPPStruct valCPPStruct;
				valCPPStruct.CPPS_GetVal(valCPPStruct, &valCPPStruct);
				valCPPStruct.CPPS_GetValToo(valCPPStruct);

				CPPClass valCPPClass;
				valCPPClass.HeyBro();

				int eVal = valCPPClass.INNER_ENUM;

                IRef(iParam);

				int a2DArray0[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};

                printf("Hi\n");
	            /*while (1)
	            {
	
				}*/

                /*asm(".intel_syntax noprefix\n"
                    "mov ebp, esp");*/

                /*double d = 2.3;
                float f = 4.5f;
                asm(//".intel_syntax noprefix\n"
                    "movsd %0, %%xmm0\n"
                    //"movupd xmm0, %0\n"
                     "addpd %%xmm0, %%xmm1\n"
                    //"addpd %1, %0\n"
                :
                : "m"(d), "m"(f)
				: "%xmm0", "%xmm1");*/

                /*asm(//".intel_syntax noprefix\n"
                    "nop\n"
                    "nop\n"
                     "nop\n"
                    //"addpd %1, %0\n"
                :
                : 
				: ); */

				struct CInnerBitField
				{
					int mField1:1;
					int mField2:1;
					int mField3:1;
					int mField4:12;
					int mField5:1;
		
					int mField6:1;
					int mField7:3;
				};

				CInnerBitField ibf;
				ibf.mField1 = 1;
				ibf.mField2 = 0;
				ibf.mField3 = 0;
				ibf.mField4 = 9;
				ibf.mField5 = 0;
				ibf.mField6 = 1;
				ibf.mField7 = 4;

				if (false)
				{
					//23|234|

					typedef int(*TestFunc)(uint32_t);
					//HMODULE module = ::LoadLibraryA("C:/proj/VisualGDB/Test1/Debug/Test1.dll");
					//HMODULE module = ::LoadLibraryA("C:/proj/TestDLL/x64/Debug/TestDLL.dll");

					uint32_t loadStart = GetTickCount();
					HMODULE module = ::LoadLibraryA("C:/Beef/IDE/dist/IDEHelper64_d.dll");
					TestFunc testFunc = (TestFunc)::GetProcAddress(module, "TimeTest");
					int outVal = testFunc(loadStart);
					printf("Freeing TestDLL module\n");
					::FreeLibrary(module);
				}

				if (false)
				{
					typedef int(*TestFunc)();
	                //HMODULE module = ::LoadLibraryA("C:/proj/VisualGDB/Test1/Debug/Test1.dll");
					HMODULE module = ::LoadLibraryA("C:/Beef/IDE/dist/BeefySysLib64_d.dll");
	                TestFunc testFunc = (TestFunc)::GetProcAddress(module, "BFApp_Create");
	                int outVal = testFunc();
	                printf("Freeing TestDLL module\n");
	                ::FreeLibrary(module);
				}

				if (false)
				{
					typedef int(*TestFunc)();
                	//HMODULE module = ::LoadLibraryA("C:/proj/VisualGDB/Test1/Debug/Test1.dll");
					HMODULE module = ::LoadLibraryA("C:/Beef/IDE/dist/TestCPP.exe");
	                TestFunc testFunc = (TestFunc)::GetProcAddress(module, "TestFunc");
	                int outVal = testFunc();
	                printf("Freeing TestDLL module\n");
	                ::FreeLibrary(module);
				}

                typedef int(*TestFunc)(int, int);
                //HMODULE module = ::LoadLibraryA("C:/proj/VisualGDB/Test1/Debug/Test1.dll");
				HMODULE module = ::LoadLibraryA("C:/proj/TestDLL/x64/Debug/TestDLL.dll");
                TestFunc testFunc = (TestFunc)::GetProcAddress(module, "TestFunc");
                int outVal = testFunc(2, 3);
                printf("Freeing TestDLL module\n");
                ::FreeLibrary(module);
                //testFunc();

                std::map<int, float> iMap;
                iMap[7] = 1.1f;
                iMap[4] = 2.1f;
                iMap[8] = 3.1f;

                for (int i = 2; i < 10; i++)
                {
                    iMap[i] = i * 0.1f;
				}

				auto itr = iMap.begin();
				while (itr != iMap.end())
				{
					auto& val = *itr;
					++itr;
				}

                std::list<int> iList;

				iList.push_back(3);
                iList.push_back(5);
                iList.push_back(6);
                iList.push_back(7);
                iList.push_back(9);
				for (int i = 3; i < 1000; i++)
				{
					iList.push_back(i);
				}

                

                std::vector<int> iVec;
                iVec.push_back(3);
                iVec.push_back(5);
                iVec.push_back(6);
                iVec.push_back(7);
                iVec.push_back(9);

				int* iPtr = &iVec[0];

                char cVals[7] = {1, 2, 3, 4, 5, 6, 7};
                int iVals[7] = {1, 2, 3, 4, 5, 6, 7};

                int iSize = (int)iList.size();

                int i = iParam + 123;
                i = CTest2(i); 
                printf("%d\n", i);
			}
		
		    void Vector2::Write()
		    {
                // Debug visualizers work for C++ on
                //  things like std::map and std::string
                //std::map<int, std::string> testMap;
                //testMap[1] = "One";
                //testMap[2] = "Two";

                // Calls back into Length property getter in Beef
			}
		}
	}
}
