#pragma warning disable 168

using System;
using System.Collections;
using System.Reflection;

namespace LibA
{
	extension LibA1 : IDisposable
	{
		public void Dispose()
		{

		}

		public static int operator<=>(Self lhs, Self rhs)
		{
			return 0;
		}
	}
}

namespace Tests
{
	class Generics
	{
		struct StructA : IDisposable
		{
			int mA = 123;

			public void Dispose()
			{

			}
		}

		class ClassA : IDisposable, LibA.IVal
		{
			int LibA.IVal.Val
			{
				get
				{
					return 123;
				}

				set
				{

				}
			}

			void IDisposable.Dispose()
			{

			}
		}

		class ClassB : IDisposable, LibA.IVal
		{
			public int Val
			{
				get
				{
					return 234;
				}

				set
				{

				}
			}

			public void Dispose()
			{

			}
		}

		class Singleton<T> where T : Singleton<T>
		{
		    public static T mInstance;

		    protected this()
		    {
		        mInstance = (T)this;
		    }
		}

		class ClassC : Singleton<ClassC>
		{
		}

		class ClassD
		{
			public static int operator<=>(Self lhs, Self rhs)
			{
				return 0;
			}
		}
		
		static void DoDispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		struct Disposer<T>
		{
			static void UseDispose(IDisposable disp)
			{

			}

			static void DoDisposeA(mut T val) where T : IDisposable
			{
				val.Dispose();
				UseDispose(val);
			}

			static void DoDisposeB(mut T val) where T : IDisposable
			{
				val.Dispose();
			}
		}

		[Test]
		public static void TestGenericDelegates()
		{
			delegate void(String v) dlg = scope => StrMethod;
			CallGenericDelegate(dlg);
			CallGenericDelegate<String>(scope => StrMethod);
		}

		public static void CallGenericDelegate<T>(delegate void(T v) dlg)
		{
		}

		public static void StrMethod(String v)
		{
		}

		public static int MethodA<T>(T val) where T : var
		{
			return 1;
		}

		public static int MethodA<T>(T val) where T : struct
		{
			return 2;
		}

		public static int MethodA<T>(T val) where T : enum
		{
			int val2 = (int)val;
			T val3 = 0;
			return 3;
		}

		public static int MethodA<T>(T val) where T : interface
		{
			return 4;
		}

		public struct Entry
		{
			public static int operator<=>(Entry lhs, Entry rhs)
			{
				return 0;
			}
		}

		public static void Alloc0<T>() where T : new, delete, IDisposable
		{
			alloctype(T) val = new T();
			val.Dispose();
			delete val;
		}

		public static void Alloc1<T>() where T : new, delete, IDisposable, Object
		{
			alloctype(T) val = new T();
			T val2 = val;
			val.Dispose();
			delete val;
		}

		public static void Alloc2<T>() where T : new, IDisposable, struct
		{
			alloctype(T) val = new T();
			T* val2 = val;
			val2.Dispose();
			delete val;
		}

		public static void Alloc3<T>() where T : new, IDisposable, struct*
		{
			T val2 = default;
			if (val2 != null)
				val2.Dispose();
			delete val2;
		}

		public class ClassE
		{
		    public static Self Instance = new ClassE() ~ delete _;

		    public int CreateSystem<T>()
		    {
		        return 999;
		    }
		}

		public void TestCast<T, TI>(T val) where T : class where TI : interface
		{
			TI iface = val as TI;
		}

		public void MethodA<T>(List<T> list)
		{

		}

		public void MethodA<T>(Span<T> list)
		{

		}

		public void MethodB<T>()
		{
			List<T> list = null;
			MethodA(list);
		}

		public static void MethodC()
		{

		}

		public static void MethodD(delegate void() dlg)
		{

		}

		public static void MethodD<T1>(delegate void(T1) dlg)
		{

		}

		static T2 MethodE<T, T2>(T val) where T : concrete, IEnumerable<T2> where T2 : operator T2 + T2
		{
			T2 total = default;
			for (var v in val)
			{
				total += v;
			}
			return total;
		}

		static int MethodF<T>(IEnumerable<T> val)
		{
			return 0;
		}

		static void MethodG<T, TBase>() where T : TBase
		{
			T val = default;
			TBase valBase = val;
		}

		public static TResult Sum<T, TElem, TDlg, TResult>(this T it, TDlg dlg)
		    where T: concrete, IEnumerable<TElem>
		    where TDlg: delegate TResult(TElem)
		    where TResult: operator TResult + TResult
		{
		    var result = default(TResult);
		    for(var elem in it)
		        result += dlg(elem);
		    return result;
		}

		struct DoublingEnumerator<TElem, TEnum> : IEnumerator<TElem>
			where TElem : operator TElem + TElem
			where TEnum : concrete, IEnumerator<TElem>
		{
			TEnum mEnum;

			public this(TEnum e)
			{
				mEnum = e;
			}

			public Result<TElem> GetNext() mut
			{
				switch (mEnum.GetNext())
				{
				case .Ok(let val): return .Ok(val + val);
				case .Err: return .Err;
				}
			}
		}

		static DoublingEnumerator<TElem, decltype(default(TCollection).GetEnumerator())> GetDoublingEnumerator<TCollection, TElem>(this TCollection it)
		    where TCollection: concrete, IEnumerable<TElem>
		    where TElem : operator TElem + TElem
		{
		    return .(it.GetEnumerator());
		}

		class ClassF
		{

		}

		class ClassG : ClassF
		{

		}

		public static void TestGen<T, TItem>(T val)
			where T : IEnumerable<TItem>
			where TItem : var
		{
		}

		public static void TestPreGen<T>()
		{
			List<int> a = default;
			TestGen(a);
		}

		public static TOut Conv<TOut, TIn>(TIn val) where TOut : operator explicit TIn
		{
			return (TOut)val;
		}

		static void MethodH<T, T2>(T val) where T2 : List<T>
		{

		}

		static void MethodI<T, T2>(T val) where comptype(typeof(T2)) : List<T>
		{

		}

		class ClassH<T, T2>
		{
			public class Inner<T3>
			{

			}
		}

		class OuterB<T>
		{
			public class Inner<T2>
			{
				public class MoreInner<T3>
				{
					public static Inner<int8> sVal;
					public static Inner<int8>.MoreInner<int16> sVal2;
				}
			}

			public static Inner<T>.MoreInner<T> sVal;
		}

		class OuterA<T, T2>
		{
			public typealias AliasA = OuterB<uint8>;
			public typealias AliasB<T3> = OuterB<T3>;

			public class Inner<T3>
			{
				public class MoreInner<T4>
				{
					public static Inner<int8> sVal;
					public static Inner<int8>.MoreInner<int16> sVal2;
				}

				public class MoreInnerB
				{

				}
			}

			public class InnerB
			{

			}

			public static OuterA<int,float>.Inner<int16> sVal;
			public static Inner<int16> sVal2;
			public static AliasA.Inner<int16> sVal3;
			public static OuterA<T, T2>.InnerB sVal4;
			public static Inner<int16>.MoreInnerB sVal5;
		}

		class OuterC
		{
			static void Do()
			{
				OuterA<int8, int16>.AliasA.Inner<int32> val1 = scope OuterB<uint8>.Inner<int32>();
				OuterA<int8, int16>.AliasB<uint8>.Inner<int32> val1b = scope OuterB<uint8>.Inner<int32>();
				OuterA<int8, int16>.AliasA.Inner<int32>.MoreInner<uint32> val2 = scope OuterB<uint8>.Inner<int32>.MoreInner<uint32>();
				OuterB<int8>.Inner<int8>.MoreInner<int8> val3 = OuterB<int8>.sVal;
				System.Collections.Dictionary<int, float> dict;
				OuterA<int8, int16>.Inner<int16>.MoreInnerB val4 = OuterA<int8, int16>.sVal5;
			}
		}

		[Test]
		public static void TestBasics()
		{
			Alloc2<StructA>();
			Alloc3<StructA*>();

			MethodD(scope => MethodC);

			List<Entry> list = scope .();
			list.Sort();
			List<float> floatList = scope .() {1, 2, 3};

			Dictionary<int, String> dict = scope .() { (1, "Foo"), [2]="Bar" };
			Test.Assert(dict[1] == "Foo");
			Test.Assert(dict[2] == "Bar");

			ClassA ca = scope .();
			ClassB cb = scope .();
			Test.Assert(LibA.LibA0.GetVal(ca) == 123);
			Test.Assert(LibA.LibA0.GetVal(cb) == 234);

			LibA.LibA0.Dispose(ca);
			LibA.LibA0.Dispose(cb);

			LibA.LibA0.Alloc<ClassA>();
			LibA.LibA0.Alloc<ClassB>();

			IDisposable iDisp = null;

			Test.Assert(MethodA("") == 1);
			Test.Assert(MethodA(1.2f) == 2);
			Test.Assert(MethodA(TypeCode.Boolean) == 3);
			Test.Assert(MethodA(iDisp) == 4);

			ClassC cc = scope .();
			Test.Assert(ClassC.mInstance == cc);

			LibA.LibA1 la1 = scope .();
			LibA.LibA1 la1b = scope .();
			LibA.LibA2.DoDispose(la1);
			Test.Assert(!LibA.LibA2.DoDispose2(la1));
			Test.Assert(la1 == la1b);
			Test.Assert(!LibA.LibA2.CheckEq(la1, la1b));

			ClassD cd = scope .();
			ClassD cd2 = scope .();
			Test.Assert(LibA.LibA2.CheckEq(cd, cd2));

			Test.Assert(ClassE.Instance.CreateSystem<int>() == 999);

			/*IEnumerable<float> ie = floatList;
			Test.Assert(
				[IgnoreErrors(true)]
				{
					Test.Assert(MethodE(ie) == 8);
					true
				} == false);*/
			Test.Assert(MethodE(floatList) == 6);
			Test.Assert(MethodF(floatList) == 0);

			Test.Assert(floatList.Sum((x) => x * 2) == 12);

			var e = floatList.GetDoublingEnumerator();
			Test.Assert(e.GetNext().Value == 2);
			Test.Assert(e.GetNext().Value == 4);
			Test.Assert(e.GetNext().Value == 6);

			Test.Assert(
				[IgnoreErrors(true)]
				{
					MethodG<ClassF, ClassG>();
					true
				} == false);
			MethodG<ClassG, ClassF>();

			Test.Assert(Conv<int...>(12.34f) == 12);
			Test.Assert(Conv<int,?>(12.34f) == 12);
			//MethodH(scope List<int>());

			var specializedType = typeof(Dictionary<int, float>.Enumerator) as SpecializedGenericType;
			Test.Assert(specializedType.UnspecializedType == typeof(Dictionary<,>.Enumerator));
			var t = typeof(Array2<>);
			t = typeof(ClassH<,>.Inner<>);
		}
	}

	class ConstGenerics
	{
		public static float GetSum<TCount>(float[TCount] vals) where TCount : const int
		{
			float total = 0;
			for (int i < vals.Count)
				total += vals[i];
			return total;
		}

		static int CheckString<T>(T str) where T : const String
		{
			const bool eq = str == "Abc";
			return T.Length;
		}

		[Test]
		public static void TestBasics()
		{
			float[5] fVals = .(10, 20, 30, 40, 50);

			float totals = GetSum(fVals);
			Test.Assert(totals == 10+20+30+40+50);
		}

		public static mixin TransformArray<Input, Output, InputSize>(Input[InputSize] array, delegate void(Input, ref Output) predicate) where InputSize : const int where Output : new, class
		{
			Output[2] output = default;
			for (int i = 0; i < array.Count; i++)
			{
				output[i] = scope:mixin Output();
				predicate(array[i], ref output[i]);
			}
			output
		}

		class Foo<T>
		{
		    public static T value;

			public class Inner<T2> where Foo<T> : Object
			{
				public static T value2;
			}
		}

		[Test]
		public static void TestSizedArrays()
		{
			int[2] arr = .(2, 4);

			delegate void(int, ref String) toString = scope (i, str) => { i.ToString(str); };

			List<String[2]> l2 = scope .();
			l2.Add(TransformArray!(arr, toString));

			Test.Assert(l2.Front[0] == "2");
			Test.Assert(l2.Front[1] == "4");

			int len = CheckString("Abc");
			Test.Assert(len == 3);
			len = CheckString<"Abcd">("Abcd");
			Test.Assert(len == 4);

			int val = 123;
			bool b = Foo<int>.value < val;
			b = Foo<int>.value > val;
			b = Foo<int>.Inner<float>.value2 < 1.2f;
			b = Foo<int>.Inner<float>.value2 > 2.3f;
		}
	}
}
