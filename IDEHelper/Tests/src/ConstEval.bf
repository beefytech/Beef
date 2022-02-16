using System;
using System.Collections;
using System.Diagnostics;

namespace Tests
{
	class ConstEval
	{
		public enum EnumA
		{
			case A;
			case B(float a, float b);
			case C(String str);

			public struct Inner
			{
				public const EnumA cVal = .C("InnerTest");
			}
		}

		struct StructA
		{
			public int32 mA;
			public int32 mB;

			public static int32 sVal = 1000;

			public this(int32 a, int32 b)
			{
				mA = a + sVal;
				mB = b;
			}
		}

		const String cStrA = "Abc";
		const String cStrB = GetStringA(cStrA, 12, 23);

		// Comptime attribute means this method will always be const-evaluated
		[Comptime]
		static String GetStringA(String str, int a, int b)
		{
			// Const-eval functions can return scope-allocated data
			return scope $"{str}:{a}:{b}";
		}

		static int32 Factorial(int32 n)
		{
		    return n <= 1 ? 1 : (n * Factorial(n - 1));
		}

		static int32 Fibonacci(int32 n)
		{
			return n <= 1 ? n : Fibonacci(n - 1) + Fibonacci(n - 2);
		}

		public static String GetTypeDesc<T>()
		{
			String str = scope .();

			T val = default;
			val.GetType().GetFullName(str);
			str.Append(" ");

			for (var fieldInfo in typeof(T).GetFields())
			{
				if (!fieldInfo.IsInstanceField)
					continue;
				if (@fieldInfo.Index > 0)
					str.Append(", ");
				fieldInfo.FieldType.GetFullName(str);
				str.Append(" ");
				str.Append(fieldInfo.Name);
			}

			return str;
		}

		public static Span<int32> GetSorted(String numList)
		{
			List<int32> list = scope .();
			for (var sv in numList.Split(','))
			{
				sv.Trim();
				if (int32.Parse(sv) case .Ok(let val))
					list.Add(val);
			}
			list.Sort();
			return list;
		}

		public static int32 MethodA()
		{
			mixin Add10(var a)
			{
				a += 10
			}

			int32 a = 1;

			void Inc()
			{
				a++;
			}

			for (int i < 2)
			{
				defer { a *= 2; }
				Inc();
				Add10!(a);
			}

			return a;
		}

		// This method can only be const evaluated
		[Comptime]
		public static int32 ConstSum(params int32[] vals)
		{
			int32 sum = 0;
			for (let val in vals)
				sum += val;
			return sum;
		}

		public static int32 MethodB()
		{
			// Returns different results depending on whether we are comptime
			return Compiler.IsComptime ? 1 : 0;
		}

		public static int32 MethodC(StructA sa = .(1, 2), (int32, int32) tup = (20, 30), int32[2] arr = .(300, 400))
		{
			return sa.mA + sa.mB + tup.0 + tup.1 + arr[0] + arr[1];
		}

		[Comptime(ConstEval=true)]
		static var StrToValue(String str)
		{
			if (str.Contains('.'))
				return float.Parse(str).Value;
			return int.Parse(str).Value;
		}

		class ClassA
		{
			public const let cVal0 = StrToValue("123");
			public const let cVal1 = StrToValue("1.23");
		}

		[Comptime]
		static int StrLenMixin(StringView str)
		{
		    mixin test()
			{
		        str.Length
		    }
		    return test!();
		}

		static String EnumAToStr(EnumA ea)
		{
			return ea.ToString(.. new .());
		}

		[Test]
		public static void TestBasics()
		{
			Test.Assert(ClassA.cVal0 == 123);
			Test.Assert(Math.Abs(ClassA.cVal1 - 1.23) < 0.0001);

			const int fac = Factorial(8);
			Test.Assert(fac == 40320);
			const int fib = Fibonacci(27);
			Test.Assert(fib == 196418);
			
			// Generated string has reference equality to equivalent literal
			Test.Assert(cStrB === "Abc:12:23");

			const String strC = GetTypeDesc<StructA>();
			Test.Assert(strC === "Tests.ConstEval.StructA int32 mA, int32 mB");

			// Const Span<T> results can be used to initialize statically-sized arrays
			const int32[?] iArr = GetSorted("8, 1, 3, 7");
			Compiler.Assert(iArr == .(1, 3, 7, 8));
			Test.Assert(iArr == .(1, 3, 7, 8));

			let val0 = [ConstEval]MethodA();
			Compiler.Assert(val0 == 70);
			let val1 = MethodA();
			// 'val1' is a read-only variable, but not const, so the following line would fail
			//Compiler.Assert(val1 == 24);
			Test.Assert(val1 == 70);

			// This method is marked as Comptime so it always evaluates as const
			let val2 = ConstSum(3, 20, 100);
			Compiler.Assert(val2 == 123);

			Test.Assert(MethodB() == 0);
			Test.Assert([ConstEval]MethodB() == 1);

			Test.Assert(MethodC() == 1753);
			Test.Assert(StrLenMixin("ABCD") == 4);

			String s1 = [ConstEval]EnumAToStr(.C("Test1"));
			Test.Assert(s1 === "C(\"Test1\")");
			const String s2 = EnumAToStr(.C("Test2"));
			Test.Assert(s2 === "C(\"Test2\")");
			const String s3 = EnumAToStr(.B(1, 2));
			Test.Assert(s3 === "B(1, 2)");
			const let s4 = EnumAToStr(EnumA.Inner.cVal);
			Test.Assert(s4 === "C(\"InnerTest\")");
		}
	}
}
