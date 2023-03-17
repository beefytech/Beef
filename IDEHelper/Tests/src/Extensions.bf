#pragma warning disable 168

using System;
using System.Collections;

namespace System.Collections
{
	extension List<T> where T : Tests.Extensions.IGetExVal
	{
		public int GetExVals()
		{
			int total = 0;
			for (var val in this)
			{
				total += val.GetExVal();
			}
			return total;
		}
	}

	extension Dictionary<TKey, TValue>
	{
		public static bool operator==(Self lhs, Self rhs)
		{
			if (lhs.mCount != rhs.mCount)
				return false;
			for (var kv in ref lhs)
			{
				if (rhs.TryGetValue(kv.key, var rhsVal))
				{
					if (*kv.valueRef != rhsVal)
						return false;
				}
				else
					return false;
			}
			return true;
		}
	}
}

namespace System.Collections
{
	extension TClass<T>
	{
		
	}
}

extension LibClassA
{
	public static int GetVal3<T>(T val)
	{
		return 31;
	}
}

namespace LibA
{
	extension LibA0
	{
		public new override int GetA()
		{
			return 3;
		}
	}

	extension LibA3
	{
		this
		{
			mA += 100;
		}
	}
}

namespace Tests
{
	class Extensions
	{
		public interface IGetExVal
		{
			int GetExVal();
		}

		class ClassA
		{
			public int32 mA;
		}

		extension ClassA
		{
			public int32 mB;
		}

		class ClassB : IHashable
		{
			public int32 mA;

			public this(int32 a)
			{
				mA = a;
			}

			public static bool operator==(ClassB lhs, ClassB rhs)
			{
				return lhs.mA == rhs.mA;
			}

			public int GetHashCode()
			{
				return mA;
			}
		}

		class ClassC
		{
			public extern void MethodA();
			public void MethodB() => MethodA();
		}

		extension ClassC
		{
			public override void MethodA() { }
		}

		class ClassD
		{
			public int mD = MethodD0() ~ MethodD0();
		    
			public int MethodD0()
			{
				return mD + 1;
			}

			public void MethodD1()
			{

			}
		}

		class ClassE : ClassD
		{
			public int mE = MethodE0();

			public int MethodE0()
			{
				return mE + 1;
			}
		}

		extension ClassE
		{

		}

		class TClassA<T> where T : IDisposable
		{
			public int32 mA = 10;

			public void Dispose()
			{
				mA += 20;
			}

			public int GetB()
			{
				return 1;
			}
		}
		
		class ClassF
		{
			public static int sVal = 3;

			public int mF0 = 1 ~
				{
					sVal += 40;
				};
		}

		extension ClassF
		{
			public int mF1 = 2 ~
				{
					sVal += 500;
				};
		}

		class ClassG : ClassF
		{
			public int mG0 = 3 ~
			{
				sVal += 6000;
			};
		}

		extension TClassA<T> where T : IGetExVal
		{
			public T mTVal;

			public T GetIt()
			{
				return mTVal;
			}

			public new int GetB()
			{
				return 2;
			}
		}

		class ImpGetExVal : IGetExVal, IDisposable
		{
			public int32 mVal = 999;

			void IDisposable.Dispose()
			{
				
			}

			int IGetExVal.GetExVal()
			{
				return mVal;
			}
		}

		class ImpDisp : IDisposable
		{
			public int32 mVal = 999;

			void IDisposable.Dispose()
			{

			}
		}

		static int UseTClass<T>(TClassA<T> val) where T : IDisposable
		{
			return -1;
		}

		static int UseTClass<T>(TClassA<T> val) where T : IDisposable, IGetExVal
		{
			return val.GetIt().GetExVal();
		}

		struct StructA
		{
		    public int mVal;
		    public int mVal2 = 111;

		    public this(int val, int val2)
		    {
		        mVal = val;
		        mVal2 = val2;
		    }
		}

		extension StructA
		{
			public int mVal3 = 222;

		    public this(int val)
		    {
		        mVal = val;
		    }
		}

		struct TestExtern<T>
		{
			public extern void MethodA();
		}

		extension TestExtern<T> where T : Char8
		{
			public override void MethodA()
			{
			}
		}

		[Test]
		public static void TestBasics()
		{
			Test.Assert(typeof(ClassA).InstanceSize == typeof(Object).InstanceSize + 4+4);

			ClassA ca = scope ClassA();
			ca.mA = 123;
			ca.mB = 234;

			ClassC cc = scope .();
			cc.MethodB();

			ClassE ce = scope .();
			Test.Assert(ce.mD == 1);
			Test.Assert(ce.mE == 1);

			///
			{
				ClassF cf = scope .();
			}
			Test.Assert(ClassF.sVal == 543);
			///
			{
				ClassF.sVal = 3;
				ClassG cg = scope .();
			}
			Test.Assert(ClassF.sVal == 6543);
			ClassF.sVal = 3;
			Object obj = new ClassF();
			delete obj;
			Test.Assert(ClassF.sVal == 543);
			ClassF.sVal = 3;
			obj = new ClassG();
			delete obj;
			Test.Assert(ClassF.sVal == 6543);

			StructA ms = .(1);
			Test.Assert((ms.mVal == 1) && (ms.mVal2 == 111) && (ms.mVal3 == 222));
			ms = .(1, 2);
			Test.Assert((ms.mVal == 1) && (ms.mVal2 == 2) && (ms.mVal3 == 222));
		}

		[Test]
		public static void TestTClass()
		{
			TClassA<ImpGetExVal> ta = scope TClassA<ImpGetExVal>();
			ta.mTVal = scope ImpGetExVal();
			int val = ta.GetIt().mVal;
			Test.Assert(val == 999);
			Test.Assert(typeof(decltype(ta)).InstanceSize == typeof(Object).InstanceSize + 4 + sizeof(Object));
			Test.Assert(UseTClass(ta) == 999);
			Test.Assert(ta.GetB() == 2);

			TClassA<ImpDisp> tb = scope TClassA<ImpDisp>();
			Test.Assert(typeof(decltype(tb)).InstanceSize == typeof(Object).InstanceSize + 4);
			Test.Assert(UseTClass(tb) == -1);
			Test.Assert(tb.GetB() == 1);
		}

		[Test]
		public static void TestList()
		{
			System.Collections.List<ImpGetExVal> list = scope .();

			ImpGetExVal val0 = scope ImpGetExVal();
			list.Add(val0);
			val0.mVal = 10;
			ImpGetExVal val1 = scope ImpGetExVal();
			list.Add(val1);
			val1.mVal = 100;

			int val = list.GetExVals();
			Test.Assert(val == 110);
		}

		[Test]
		public static void TestDictionary()
		{
			Dictionary<String, int> dictLhs = scope .() { ("Abc", 123), ("Def", 234) };
			Dictionary<String, int> dictRhs = scope .() { (scope:: String("Abc"), 123), ("Def", 234) };

			Test.Assert(dictLhs == dictRhs);
			Test.Assert(!LibA.LibA0.DictEquals(dictLhs, dictRhs));

			Dictionary<String, int>[2] dictArrLhs = .(dictLhs, dictLhs);
			Dictionary<String, int>[2] dictArrRhs = .(dictRhs, dictRhs);
			Test.Assert(dictArrLhs != dictArrRhs);

			Dictionary<ClassB, int> dictLhs2 = scope .() { (scope:: ClassB(111), 123) };
			Dictionary<ClassB, int> dictRhs2 = scope .() { (scope:: ClassB(111), 123) };
			Test.Assert(dictLhs2 == dictRhs2);

			Dictionary<ClassB, int>[2] dictArrLhs2 = .(dictLhs2, dictLhs2);
			Dictionary<ClassB, int>[2] dictArrRhs2 = .(dictRhs2, dictRhs2);
			Test.Assert(dictArrLhs2 == dictArrRhs2);
		}

		[Test]
		public static void TestSharedData()
		{
			Test.Assert(LibClassA.sMagic == 1111);

			LibClassA ca = scope LibClassA();
			Test.Assert(LibClassA.sMagic == 2221);
			Test.Assert(ca.mA == 107);
			Test.Assert(ca.LibB_GetB() == 108);
			Test.Assert(ca.LibC_GetB() == 13);
			Test.Assert(ca.GetVal2() == 9);

			ca = scope LibClassA(12345);
			Test.Assert(LibClassA.sMagic == 3331);
			Test.Assert(ca.mA == 7);
			Test.Assert(ca.LibB_GetB() == 1008);
			Test.Assert(ca.LibC_GetB() == 13);

			ca = scope LibClassA((int8)2);
			Test.Assert(LibClassA.sMagic == 4441);
			Test.Assert(ca.mA == 7);
			Test.Assert(ca.LibB_GetB() == 8);
			Test.Assert(ca.LibC_GetB() == 30013);

			ca = LibClassA.Create();
			Test.Assert(LibClassA.sMagic == 5551);
			Test.Assert(ca.mA == 107);
			delete ca;
			Test.Assert(LibClassA.sMagic == 7771);

			LibA.LibA0 la0 = scope .();
			int la0a = la0.GetA();
			Test.Assert(la0a == 3);

			LibA.LibA3 la3 = scope .();
			Test.Assert(la3.mA == 114);
			Test.Assert(la3.mB == 7);
			LibA.LibA4 la4 = scope .();
			Test.Assert(la4.mA == 10);
		}

		[Test]
		public static void TestExtensionOverride()
		{
			int a = 123;
			int directResult = LibClassA.GetVal3(a);
			Test.Assert(directResult == 31);
			// This should only call the LibA version since the LibA:LibClassB.DoGetVal3 won't have
			//  access to call the Tests:LibClassB.DoGetVal3
			int indirectResult = LibClassB.DoGetVal3(a);
			Test.Assert(indirectResult == 30);
		}
	}
}
