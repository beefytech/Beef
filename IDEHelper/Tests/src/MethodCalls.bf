using System;
using System.Collections;
using System.Numerics;

namespace Tests
{
	class MethodCalls
	{
		struct StructA
		{
			public int mA = 123;
			public int mB = 234;

			public this()
			{

			}

			public this(int a, int b)
			{
				mA = a;
				mB = b;
			}
		}

		struct StructB
		{
			public int mA = 10;
			public int mB = 20;
			public int mC = 30;
			public int mD = 40;

			public this(int a, int b, int c, int d)
			{
				mA = a;
				mB = b;
				mC = c;
				mD = d;
			}
		}

		struct StructC
		{
			public int32[8] mData;
		}

		static int Method0(StructA val)
		{
			return val.mA;
		}

		static int Method1(StructA val, StructA val2)
		{
			return val2.mA;
		}

		static int Method2(StructA val, StructA val2, StructA val3)
		{
			return val3.mA;
		}

		static StructA Method3(StructA val)
		{
			return val;
		}

		static StructA Method4(StructA val, StructA val2)
		{
			return val2;
		}

		static StructA Method5(StructA val, StructA val2, StructA val3)
		{
			return val3;
		}

		int Method0b(StructA val)
		{
			return val.mA;
		}

		int Method1b(StructA val, StructA val2)
		{
			return val2.mA;
		}

		int Method2b(StructA val, StructA val2, StructA val3)
		{
			return val3.mA;
		}

		StructA Method3b(StructA val)
		{
			return val;
		}

		StructA Method4b(StructA val, StructA val2)
		{
			return val2;
		}

		StructA Method5b(StructA val, StructA val2, StructA val3)
		{
			return val3;
		}

		public static float AddFloats<C>(params float[C] vals) where C : const int
		{
			float total = 0;
			for (var val in vals)
				total += val;
			return total;
		}

		struct Valueless
		{
		}

		public static void InCallPtr(in char8* val)
		{
		}

		public static void InCallObj(in Object val)
		{
		
		}

		public static void InCallValueless(in Valueless val)
		{
		}

		public static int InCallStruct(in StructA val)
		{
			return val.mA + val.mB;
		}

		public static int ParamsA(int a, params Span<int> ints)
		{
			int result = a;
			for (var i in ints)
				result += i;
			return result;
		}

		public static int ParamsB(int a, params int[] ints)
		{
			return ParamsA(a, params ints);
		}

		static void ModifyC(StructC sc)
		{
#unwarn
			var scPtr = &sc;
			scPtr.mData[0] += 100;
		}

		[CallingConvention(.Cdecl)]
		static void ModifyC2(StructC sc)
		{
#unwarn
			var scPtr = &sc;
			scPtr.mData[0] += 100;
		}

		static void ObjMethod(Object obj)
		{

		}


		static void InInt(in int a)
		{
			Test.Assert(a == 123);
#unwarn
			int* aPtr = &a;
			*aPtr = 234;
		}

		static void CopyStructA(StructA sa)
		{
#unwarn
			StructA* saPtr = &sa;
			saPtr.mA += 1000;
		}

		static void InStructA(in StructA sa)
		{
#unwarn
			StructA* saPtr = &sa;
			saPtr.mA += 1000;
		}

		static int Named(int p1, float p2, int p3)
		{
			return 10000 + p1*100 + (int)p2*10 + p3;
		}

		static int Named(int p0, int p1, float p2)
		{
			return 20000 + p0*100 + p1*10 + (.)p2;
		}

		static int Named(int p0=1, int p1=2, double p2=3)
		{
			return 30000 + p0*100 + p1*10 + (.)p2;
		}

		static int sIdx = 0;
		static int GetNext() => ++sIdx;

		public static float ParamsTest(params Span<float> span)
		{
			return span[0];
		}

		public static T ParamsTest2<T>(params Span<T> span)
		{
			return span[0];
		}

		public static float GetFirstFloat(float[3] fVals)
		{
			return fVals[0];
		}

		public static float GetFirstFloatRef(ref float[3] fVals)
		{
			return fVals[0];
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa = .(100, 101);
			StructA sa2 = .(200, 201);
			StructA sa3 = .(300, 301);

			Test.Assert(InCallStruct(sa) == 201);
			Test.Assert(InCallStruct(.(200, 202)) == 402);

			Test.Assert(Method0(sa) == 100);
			Test.Assert(Method1(sa, sa2) == 200);
			Test.Assert(Method2(sa, sa2, sa3) == 300);
			Test.Assert(Method3(sa) == sa);
			Test.Assert(Method4(sa, sa2) == sa2);
			Test.Assert(Method5(sa, sa2, sa3) == sa3);

			MethodCalls self = scope .();
			Test.Assert(self.Method0b(sa) == 100);
			Test.Assert(self.Method1b(sa, sa2) == 200);
			Test.Assert(self.Method2b(sa, sa2, sa3) == 300);
			Test.Assert(self.Method3b(sa) == sa);
			Test.Assert(self.Method4b(sa, sa2) == sa2);
			Test.Assert(self.Method5b(sa, sa2, sa3) == sa3);

			Test.Assert(AddFloats(1.0f, 2, 3) == 6.0f);

			InCallPtr("ABC");
			InCallObj("Hey");
			Valueless valueless;
			InCallValueless(valueless);

			Test.Assert(ParamsA(100, 20, 3) == 123);
			Test.Assert(ParamsB(100, 20, 3) == 123);

			StructC sc = .();
			sc.mData[0] = 123;
			ModifyC(sc);
			Test.Assert(sc.mData[0] == 223);
			ModifyC2(sc);
			Test.Assert(sc.mData[0] == 223);
			function void (StructC) scFunc = => ModifyC;
			scFunc(sc);
			Test.Assert(sc.mData[0] == 323);
			function [CallingConvention(.Cdecl)] void (StructC) scFunc2 = => ModifyC2;
			scFunc2(sc);
			Test.Assert(sc.mData[0] == 323);

			var v = ObjMethod(.. scope String());
			Test.Assert(v.GetType() == typeof(String));

			int b = 123;
			InInt(b);
			Test.Assert(b == 234);

			StructA sa4 = .(400, 401);
			CopyStructA(sa4);
			Test.Assert(sa4.mA == 400);
			InStructA(sa4);
			Test.Assert(sa4.mA == 1400);

			Test.Assert(Named(1, 2, p3:3) == 10123);
			Test.Assert(Named(p1: 1, 2, p3: 3) == 10123);
			Test.Assert(Named(p0: 1, 2, 3) == 20123);
			Test.Assert(Named(1, p1:2, 3) == 20123);
			Test.Assert(Named(p3:GetNext(), p2:GetNext(), p1:GetNext()) == 10321);
			Test.Assert(Named(p2:GetNext(), p1:GetNext(), p0:GetNext()) == 20654);
			Test.Assert(Named(p1:9) == 30193);

			List<float> fList = scope .();
			fList.Add(1.2f);
			Test.Assert(ParamsTest(params fList) == 1.2f);
			Test.Assert(ParamsTest2(params fList) == 1.2f);

			float4 fVals = .(123, 234, 345, 456);
			Test.Assert(GetFirstFloat(*(.)&fVals) == 123);
			Test.Assert(GetFirstFloatRef(ref *(.)&fVals) == 123);
		}
	}
}
