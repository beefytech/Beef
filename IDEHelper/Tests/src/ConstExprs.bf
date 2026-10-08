#pragma warning disable 168
using System;
using System.Reflection;
namespace Tests
{
	class ConstExprs
	{
		enum EnumA
		{
			A,
			B,
			C
		}

		class ClassA<T, TSize> where TSize : const int
		{
			public int GetVal()
			{
				return TSize;
			}
		}

		class ClassB<T, TSize> where TSize : const int8
		{
			ClassA<T, TSize> mVal = new ClassA<T, const TSize>() ~ delete _;
			var mVal2 = new ClassA<T, const TSize + 100>() ~ delete _;
			
			public int GetVal()
			{
				return mVal.GetVal(); 
			}

			public int GetVal2()
			{
				return mVal2.GetVal();
			}
		}

		class ClassC<TEnum> where TEnum : const EnumA
		{
			public int Test()
			{
				EnumA ea = TEnum;
				if (TEnum == .A)
				{
					return 1;
				}
				return 0;
			}
		}

		struct TestRangedArray<T, TRange> where TRange : const var
		{
			[OnCompile(.TypeInit), Comptime]
			static void TypeInit()
			{
				if (TRange is var)
					return;

				int rangeStart = 0;
				int rangeEnd = 0;

				if (ClosedRange range = TRange as ClosedRange?)
				{
					rangeStart = range.Start;
					rangeEnd = range.End+1;
				}
				else if (Range range = TRange as Range?)
				{
					rangeStart = range.Start;
					rangeEnd = range.End;
				}
				else
				{
					Compiler.EmitTypeBody(typeof(Self), scope $"""
						public const String cError = "Invalid type: {TRange}";
						""");
					return;
				}

				Compiler.EmitTypeBody(typeof(Self), scope $"""
					public const int cRangeStart = {rangeStart};
					public const int cRangeEnd = {rangeEnd};
					public T[{rangeEnd-rangeStart}] mData;
					""");
			}
		}

		struct SpecialId : int
		{
			public const Self CONST = (.)(int)(void*)(char8*)"ABC";
			public static Self operator implicit(String str) => (.)(int)(void*)(char8*)str;
		}

		public static void TestStr(SpecialId specialId)
		{
			char8* ptr = (.)(void*)(int)specialId;

			StringView sv = .(ptr);
			Test.Assert(sv == "ABC");
		}

		[Test]
		public static void TestBasics()
		{
			ClassB<float, const 123> cb = scope .();
			Test.Assert(cb.GetVal() == 123);
			Test.Assert(cb.GetVal2() == 223);

			ClassB<float, const -45> cb2 = scope .();
			Test.Assert(cb2.GetVal() == -45);
			Test.Assert(cb2.GetVal2() == 55);

			ClassC<const EnumA.A> cc = scope .();
			Test.Assert(cc.Test() == 1);

			Test.Assert(TestRangedArray<int32, -3...3>.cRangeEnd - TestRangedArray<int32, -3...3>.cRangeStart == 7);
			Test.Assert(TestRangedArray<int32, -3...>.cError == "Invalid type: -3...^1");

			TestStr(.CONST);
			TestStr("ABC");
		}

		[Reflect(.Type), AlwaysInclude]
		struct ConstValueHolder<TValue> where TValue : const var
		{
			public int mDummy;
		}

		struct SmallPair
		{
			public int16 mA;
			public int16 mB;

			public this(int16 a, int16 b)
			{
				mA = a;
				mB = b;
			}
		}

		const SmallPair cSmallPair = .(1, 2);

		static ConstExprType GetConstArg(Type type) => (type as SpecializedGenericType)?.GetGenericArg(0) as ConstExprType;

		// A struct const value's type data (and mangled name) used the heap address of its bytes, which changed every
		//  build. A struct that fits is now stored by value, and a larger one as 0
		[Test]
		public static void TestStructConstArgData()
		{
			Test.Assert(GetConstArg(typeof(ConstValueHolder<5>)).ValueData == 5);
			Test.Assert(GetConstArg(typeof(ConstValueHolder<const cSmallPair>)).ValueData == 0x0002'0001);
			// ClosedRange holds two ints, so it only fits on 32-bit
			ClosedRange range = -3...3;
			int64 rangeData = 0;
			if (sizeof(ClosedRange) <= sizeof(int64))
				Internal.MemCpy(&rangeData, &range, sizeof(ClosedRange));
			Test.Assert(GetConstArg(typeof(ConstValueHolder<-3...3>)).ValueData == rangeData);
		}

		// A String const value's type data held the literal's compiler id, which String.GetById then used as an index into
		//  the runtime literal table, so naming the type read the wrong entry or past the end of the table
		[Test]
		public static void TestStringConstArgData()
		{
			let constArg = GetConstArg(typeof(ConstValueHolder<"const arg string">));
			Test.Assert(String.GetById((.)constArg.ValueData) == "const arg string");
			Test.Assert(constArg.GetFullName(.. scope .()) == "const \"const arg string\"");
		}
	}
}
