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
				emit.Append("\\n\");");
				Compiler.EmitMethodEntry(method, emit);

				if (var genericType = method.ReturnType as SpecializedGenericType)
				{
					if ((genericType.UnspecializedType == typeof(Result<>)) || (genericType.UnspecializedType == typeof(Result<,>)))
					{
						Compiler.EmitMethodExit(method, """
							if (@return case .Err)
							LogAttribute.gLog.AppendF($"Error: {@return}");
							""");
					}
					
				}
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

		[IFaceA("C", InitVal=345)]
		struct StructA
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

		enum MethodAErr
		{
			ErrorA,
			ErrorB
		}

		[Log]
		static Result<int, MethodAErr> MethodA(int a, int b)
		{
			return .Err(.ErrorB);
		}

		static Type GetBiggerType(Type t)
		{
			switch (t)
			{
			case typeof(int8): return typeof(int16);
			case typeof(int16): return typeof(int32);
			case typeof(int32): return typeof(int64);
			case typeof(float): return typeof(double);
			}
			return null;
		}

		static TTo GetBigger<TFrom, TTo>(TFrom val) where TTo : comptype(GetBiggerType(typeof(TFrom))), operator explicit TFrom
		{
			return (.)val;
		}

		public struct TypePrinter<T>
		{
			const int cFieldCount = GetFieldCount();
			const (int32, StringView)[cFieldCount] cMembers = Make();

			static int GetFieldCount()
			{
				int fieldCount = 0;
				for (let field in typeof(T).GetFields())
					if (!field.IsStatic)
						fieldCount++;
				return fieldCount;
			}
			
			static decltype(cMembers) Make()
			{
				if (cFieldCount == 0)
					return default(decltype(cMembers));

				decltype(cMembers) fields = ?;

				int i = 0;
				for (let field in typeof(T).GetFields())
				{
					if (!field.IsStatic)
						fields[i++] = (field.MemberOffset, field.Name);
				}

				return fields;
			}

			public override void ToString(String strBuffer)
			{
				for (var t in cMembers)
				{
					if (@t != 0)
						strBuffer.Append("\n");
					strBuffer.AppendF($"{t.0} {t.1}");
				}
			}
		}

		struct TestType
		{
			public float mX;
			public float mY;
			public float mZ;
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

			StructA sa = .();
			Test.Assert(sa.mA == 123);
			Test.Assert(sa.mB == 234);
			Test.Assert(sa.GetValB() == 234);
			Test.Assert(sa.mC == 345);
			Test.Assert(sa.GetValC() == 345);

			Compiler.Mixin("int val = 99;");
			Test.Assert(val == 99);

			MethodA(34, 45).IgnoreError();
			Debug.Assert(LogAttribute.gLog == "Called Tests.Comptime.MethodA(int a, int b) 34 45\nError: Err(ErrorB)");

			var v0 = GetBigger((int8)123);
			Test.Assert(v0.GetType() == typeof(int16));

			String str = scope .();
			TypePrinter<TestType> tp = .();
			tp.ToString(str);
			Debug.Assert(str == """
				0 mX
				4 mY
				8 mZ
				""");
		}
	}
}
