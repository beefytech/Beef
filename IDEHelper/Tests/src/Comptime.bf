using System;
using System.Diagnostics;
using System.Reflection;
using System.Collections;

namespace Tests
{
	class Comptime
	{
		[AttributeUsage(.All)]
		struct IFaceAAttribute : Attribute, IComptimeTypeApply
		{
			String mMemberName;
			int32 mInitVal;

			public int32 InitVal
			{
				set mut
				{
					mInitVal = value;
				}
			}

			public this(String memberName)
			{
				mMemberName = memberName;
				mInitVal = 0;
			}

			[Comptime]
			public void ApplyToType(Type type)
			{
				Compiler.EmitTypeBody(type, scope $"""
					public int32 m{mMemberName} = {mInitVal};
					public int32 GetVal{mMemberName}() => mC;
					""");
			}
		}

		[AttributeUsage(.Method)]
		struct LogAttribute : Attribute, IComptimeMethodApply
		{
			public static String gLog = new .() ~ delete _;

			[Comptime] 
			public void ApplyToMethod(ComptimeMethodInfo method)
			{
				String emit = scope $"LogAttribute.gLog.AppendF($\"Called {method}";
				for (var fieldIdx < method.ParamCount)
					emit.AppendF($" {{ {method.GetParamName(fieldIdx)} }}");
				emit.Append("\");");
				Compiler.EmitMethodEntry(method, emit);
			}
		}

		[IFaceA("C", InitVal=345)]
		class ClassA
		{
			public int mA = 123;

			[OnCompile(.TypeInit), Comptime]
			public static void Generate()
			{
				Compiler.EmitTypeBody(typeof(Self), """
					public int32 mB = 234;
					public int32 GetValB() => mB;
					""");
			}
		}

		[Log]
		public static void MethodA(int a, int b)
		{

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

		static Type GetConcreteEnumerator<TCollection, TElem>() where TCollection : IEnumerable<TElem>
		{
			TCollection col = ?;
			return col.GetEnumerator().GetType();
		}

		public static DoublingEnumerator<TElem, comptype(GetConcreteEnumerator<TCollection, TElem>())> GetDoublingEnumerator<TCollection, TElem>(this TCollection it)
		    where TCollection: concrete, IEnumerable<TElem>
		    where TElem : operator TElem + TElem
		{
		    return .(it.GetEnumerator());
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			Test.Assert(ca.mA == 123);
			Test.Assert(ca.mB == 234);
			Test.Assert(ca.GetValB() == 234);
			Test.Assert(ca.mC == 345);
			Test.Assert(ca.GetValC() == 345);

			Compiler.Mixin("int val = 99;");
			Test.Assert(val == 99);

			MethodA(34, 45);
			Debug.Assert(LogAttribute.gLog == "Called Tests.Comptime.MethodA(int a, int b) 34 45");

			List<float> fList = scope .() { 1, 2, 3 };
			var e = fList.GetDoublingEnumerator();
			Test.Assert(e.GetNext().Value == 2);
			Test.Assert(e.GetNext().Value == 4);
			Test.Assert(e.GetNext().Value == 6);

			//Test.Assert(fList.DoubleVals() == 12);
		}
	}
}
