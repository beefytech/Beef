#pragma warning disable 168

using System;

namespace Tests
{
	class Operators
	{
		struct FlagsRegister
		{
			public bool zero;
			public bool subtract;
			public bool half_carry;
			public bool carry;

			const uint8 ZERO_FLAG_BYTE_POSITION = 7;
			const uint8 SUBTRACT_FLAG_BYTE_POSITION = 6;
			const uint8 HALF_CARRY_FLAG_BYTE_POSITION = 5;
			const uint8 CARRY_FLAG_BYTE_POSITION = 4;

			public this(bool z, bool s, bool hc, bool c)
			{
				zero = z;
				subtract = s;
				half_carry = hc;
				carry = c;
			}

			//Convert flag register to a uint8
			public static implicit operator uint8 (FlagsRegister flag)
			{
				return (flag.zero 		  ? 1 : 0)	<< ZERO_FLAG_BYTE_POSITION |
					   (flag.subtract     ? 1 : 0)  << SUBTRACT_FLAG_BYTE_POSITION |
					   (flag.half_carry   ? 1 : 0)	<< HALF_CARRY_FLAG_BYTE_POSITION |
					   (flag.carry        ? 1 : 0) 	<< CARRY_FLAG_BYTE_POSITION;
			};
		}

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

			public static StructA operator++(StructA val)
			{
				StructA newVal;
				newVal.mA = val.mA + 1;
				return newVal;
			}

			[Commutable]
			public static bool operator<(StructA lhs, StructB rhs)
			{
				return lhs.mA < rhs.mB;
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

			public void operator++() mut
			{
				mB++;
			}
		}

		struct StructC
		{
			public int mA;
			public int mB;

			public this(int a, int b)
			{
				mA = a;
				mB = b;
			}

			public static bool operator==(StructC lhs, StructC rhs)
			{
				return lhs.mA == rhs.mA;
			}
		}

		struct StructD
		{
			int mVal;

			public this(int val)
			{
				mVal = val;
			}

			public static implicit operator int(StructD x)
			{
			    return x.mVal;
			}

		    public static implicit operator StructD(int x)
		    {
		        return default(StructD);
		    }
		}

		struct StructE
		{
			float mVal;

			public this(float val)
			{
				mVal = val;
			}

			public static implicit operator float(StructE x)
			{
			    return x.mVal;
			}

		    public static implicit operator StructE(float x)
		    {
		        return .(x);
		    }

			public static StructE operator+(StructE lhs, StructE rhs)
			{
				return .(1);
			}

			public static int operator+(StructE lhs, int rhs)
			{
				return 2;
			}

			public static int operator+(int lhs, StructE rhs)
			{
				return 3;
			}
		}

		struct StructF
		{
			public static int operator+(StructF lhs, int rhs)
			{
				return 1;
			}

			public static int operator+(int lhs, StructF rhs)
			{
				return 2;
			}

			[Commutable]
			public static int operator+(StructF lhs, float rhs)
			{
				return 3;
			}
		}

		struct StructG : this(int a)
		{
			public static StructG operator+(ref StructG lhs, ref StructG rhs)
			{
				lhs.a += 1000;
				rhs.a += 1000;

				return .(lhs.a + rhs.a);
			}

			public static StructG operator-(ref StructG val)
			{
				val.a += 1000;
				return val;
			}

			public static implicit operator int(ref StructG val)
			{
				return val.a;
			}
		}

		struct StructH
		{
			public static int operator+(StructH lhs, int rhs)
			{
				return 1;
			}

			public static int operator+(int lhs, StructH rhs)
			{
				return 2;
			}

			[Commutable]
			public static int operator+(StructH lhs, float rhs)
			{
				return 3;
			}

			public static int operator&+(StructH lhs, int rhs)
			{
				return 4;
			}

			public static int operator&+(int lhs, StructH rhs)
			{
				return 5;
			}

			[Commutable]
			public static int operator&+(StructH lhs, float rhs)
			{
				return 6;
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

			public T GetInt() where T : Int32
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

		public static T Complex<T, T2, T3>(T val, T2 val2, T3 val3)
			where T : operator -T
			where T : operator implicit T2
			where T : operator T + T2
			where int32 : operator T + T3
		{
			T conv = val2;
			T result = val + val2;
			result = -result;
			int32 iRes = val + val3;
			return result;
		}

		struct StructOp3<T, T2> where float : operator T + T2
		{
			public float Use(T lhs, T2 rhs)
			{
				float f = lhs + rhs;
				return f;
			}
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

			Test.Assert(sa1 < sb0);
			Test.Assert(!(sa1 >= sb0));

			Test.Assert(sb0 > sa1);
			Test.Assert(!(sb0 <= sa1));

			StructA sa3 = sa0 + sb0;
			Test.Assert(sa3.mA == 1012);

			StructA sa4 = Op(sa0, sb0);
			Test.Assert(sa4.mA == 1012);

			StructA sa6 = sa0++;
			Test.Assert(sa0.mA == 2);
			Test.Assert(sa6.mA == 1);
			sa6 = ++sa0;
			Test.Assert(sa0.mA == 3);
			Test.Assert(sa6.mA == 3);

			StructB sb6 = sb0++;
			Test.Assert(sb0.mB == 12);
			Test.Assert(sb6.mB == 11);
			sb6 = ++sb0;
			Test.Assert(sb0.mB == 13);
			Test.Assert(sb6.mB == 13);

			float val = Op((int32)100, (int16)23);
			Test.Assert(val == 123);

			int32 i32res = Complex((int32)100, (int16)23, (int8)4);
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

			Result<StructC> rsc = .Ok(.(123, 234));
			Result<StructC> rsc2 = .Ok(.(123, 345));
			Test.Assert(rsc == rsc2);
			Test.Assert(rsc !== rsc2);

			StructOp3<int16, int32> so3 = .();
			float f = so3.Use(1, 2);
			Test.Assert(f == 3);

			FlagsRegister fr = .(true, false, true, true);
			Test.Assert(fr == (uint8)0b10110000);

			StructD sd = .(9);
			Test.Assert(sd + 10 == 19);
			Test.Assert(10 + sd == 19);

			StructE se = .(9);
			Test.Assert(se + 10 == 2);
			Test.Assert(10 + se == 3);
			Test.Assert(se + 1.2f == 1);
			Test.Assert(1.2f + se == 1);

			StructF sf = default;
			Test.Assert(sf + 10 == 1);
			Test.Assert(10 + sf == 2);
			Test.Assert(sf + 1.0f == 3);
			Test.Assert(2.0f + sf == 3);
			Test.Assert(sf &+ 10 == 1);
			Test.Assert(10 &+ sf == 2);
			Test.Assert(sf &+ 1.0f == 3);
			Test.Assert(2.0f &+ sf == 3);

			StructH sh = default;
			Test.Assert(sh + 10 == 1);
			Test.Assert(10 + sh == 2);
			Test.Assert(sh + 1.0f == 3);
			Test.Assert(2.0f + sh == 3);
			Test.Assert(sh &+ 10 == 4);
			Test.Assert(10 &+ sh == 5);
			Test.Assert(sh &+ 1.0f == 6);
			Test.Assert(2.0f &+ sh == 6);

			StructG sg = .(100);
			StructG sg2 = .(200);
			var sg3 = sg + sg2;
			var sg4 = -sg3;
			Test.Assert(sg.a == 1100);
			Test.Assert(sg2.a == 1200);
			Test.Assert(sg3.a == 3300);
			Test.Assert(sg4.a == 3300);

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

			public void operator++() mut
			{
				mA++;
			}
		}

		

		[Test]
		public static void TestCompareWithCastOperator()
		{
			IntStruct iVal = .();
			Test.Assert(iVal == 123);
			Test.Assert(iVal == 123.0f);

			var iVal2 = iVal++;
			var iVal3 = ++iVal;
			++iVal;

			Test.Assert(iVal == 126);
			Test.Assert(iVal2 == 123);
			Test.Assert(iVal3 == 125);
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

