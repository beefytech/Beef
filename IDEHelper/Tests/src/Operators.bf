#pragma warning disable 168

using System;

namespace Tests
{
	class Operators
	{
		struct StructA
		{
			public int mA;

			public static StructA operator+(StructA lhs, StructA rhs)
			{
				StructA res;
				res.mA = lhs.mA + rhs.mA;
				return res;
			}

			public static StructA operator-(StructA lhs, StructA rhs)
			{
				StructA res;
				res.mA = lhs.mA - rhs.mA;
				return res;
			}
		}

		struct StructB
		{
			public int mB;

			public static StructA operator+(StructA sa, StructB sb)
			{
				StructA result;
				result.mA = sa.mA + sb.mB + 1000;
				return result;
			}
		}

		struct StructOp<T, T2> where T : operator T + T2
		{
			public T DoIt(T val, T2 val2)
			{
				return val + val2;
			}
		}

		struct StructOp2<T>
		{
			public T mVal;

			public static T operator+<T2>(StructOp2<T> lhs, T2 rhs) where T : operator T + T2
			{
				return lhs.mVal + rhs;
			}

			public static T operator|<T2>(StructOp2<T> lhs, T2 rhs) where T : operator implicit T2
			{
				T temp = rhs;
				return temp;
			}

			public T GetNeg<T2>(T2 val) where T : operator -T2
			{
				return -val;
			}

			public T GetInt() where T : Int
			{
				return mVal;
			}
		}

		/*struct OuterOp<T>
		{
			public struct InnerOp<T2>
				where T : operator T + T2
				where T : operator -T
				where T : operator implicit T2
			{
				public static T Op(T val, T2 val2)
				{
					return val + val2;
				}

				public static T Neg(T val)
				{
					return -val;
				}

				public static T Cast(T2 val)
				{
					return val;
				}

				struct InnerOp2
				{
					public static T Op(T val, T2 val2)
					{
						return val + val2;
					}

					public static T Neg(T val)
					{
						return -val;
					}

					public static T Cast(T2 val)
					{
						return val;
					}
				}

				struct InnerOp3<T3>
				{
					public static T Op(T val, T2 val2)
					{
						return val + val2;
					}

					public static T Neg(T val)
					{
						return -val;
					}

					public static T Cast(T2 val)
					{
						return val;
					}
				}
			}
		}

		struct OuterOp2<T>
		{
			public struct InnerOp<T2>
			{

			}

			extension InnerOp<T2>
				where T : operator T + T2
				where T : operator -T
			{
				public static T Op(T val, T2 val2)
				{
					return val + val2;
				}

				public static T Neg(T val)
				{
					return -val;
				}
			}
		}*/

		public static T Op<T, T2>(T val, T2 val2) where T : operator T + T2
		{
			return val + val2;
		}

		public static T Complex<T, T2>(T val, T2 val2)
			where T : operator T + T2
			where T : operator -T
			where T : operator implicit T2
		{
			T conv = val2;
			T result = val + val2;
			result = -result;
			return result;
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa0 = default;
			sa0.mA = 1;
			StructA sa1 = default;
			sa1.mA = 2;

			StructA sa2 = sa0 + sa1;
			Test.Assert(sa2.mA == 3);

			StructB sb0;
			sb0.mB = 11;
			StructB sb1;
			sb1.mB = 12;

			StructA sa3 = sa0 + sb0;
			Test.Assert(sa3.mA == 1012);

			StructA sa4 = Op(sa0, sb0);
			Test.Assert(sa4.mA == 1012);

			float val = Op((int32)100, (int16)23);
			Test.Assert(val == 123);

			int32 i32res = Complex((int32)100, (int16)23);
			Test.Assert(i32res == -123);

			StructOp<StructA, StructB> sOp;
			let sa5 = sOp.DoIt(sa1, sb1);
			Test.Assert(sa5.mA == 1014);

			StructOp2<int32> sOp2;
			sOp2.mVal = 100;
			int32 res6 = sOp2 + (int16)40;
			Test.Assert(res6 == 140);
			int32 res7 = sOp2.GetInt();
			Test.Assert(res7 == 100);

			/*let oai = OuterOp<float>.InnerOp<int>.Op(1.0f, 100);
			Test.Assert(oai == 101.0f);

			let oai2 = OuterOp2<float>.InnerOp<int>.Op(2.0f, 200);
			Test.Assert(oai2 == 202.0f);*/
		}

		struct IntStruct
		{
			public int mA = 123;

			public typealias ZaffInt = int;

			public ZaffInt GetIt()
			{
				return 123;
			}

			public static implicit operator int(Self val)
			{
				return val.mA;
			}

			public static implicit operator Self(int val)
			{
				IntStruct sVal;
				sVal.mA = val;
				return sVal;
			}
		}

		[Test]
		public static void TestCompareWithCastOperator()
		{
			IntStruct iVal = .();
			Test.Assert(iVal == 123);
			Test.Assert(iVal == 123.0f);
		}

		const String cStrD = "D";
		const char8* cStrPD = "D";

		public static void TestDefaults(StringView sv = "ABC", IntStruct intStruct = 123)
		{
			Test.Assert(sv == "ABC");
			Test.Assert(intStruct.mA == 123);
		}

		[Test]
		public static void TestStringOp()
		{
			const String cStr1 = "A" + "B";
			const String cStr2 = cStr1 + "C" + cStrD;
			Test.Assert(cStr2 == "ABCD");
			Test.Assert(cStr2 === "ABCD");

			const char8* cStr3 = "A" + "B";
			const char8* cStr4 = cStr1 + "C" + cStrPD;
			Test.Assert(StringView(cStr4) == "ABCD");

			TestDefaults();

			String strA = scope String("ABCD");
			Test.Assert(strA == cStr2);
			Test.Assert(strA !== cStr1);

			let strTup = (strA, strA);
			Test.Assert(strTup == (cStr2, cStr2));
			Test.Assert(strTup !== (cStr2, cStr2));
		}

		public static TTo Convert<TFrom, TTo>(TFrom val) where TTo : operator explicit TFrom
		{
			return (TTo)val;
		}

		[Test]
		public static void TestConversion()
		{
			int a = 123;
			float f = Convert<int, float>(a);
		}
	}
}
