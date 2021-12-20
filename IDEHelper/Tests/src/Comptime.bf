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
			public static StructA sSA;
			public const StructA cSA = .();

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
		
		class SerializationContext
		{
			public String mStr = new String() ~ delete _;
			public void Serialize<T>(String name, T val) where T : struct
			{
				mStr.AppendF($"{name} {val}\n");
			}
		}

		interface ISerializable
		{
			void Serialize(SerializationContext ctx);
		}

		[AttributeUsage(.Enum | .Struct | .Class, .NotInherited | .ReflectAttribute | .DisallowAllowMultiple)]
		struct SerializableAttribute : Attribute, IComptimeTypeApply
		{
			[Comptime]
			public void ApplyToType(Type type)
			{
				const String SERIALIZE_NAME = "void ISerializable.Serialize(SerializationContext ctx)\n";

				String serializeBuffer = new .();

				Compiler.Assert(!type.IsUnion);

				for (let field in type.GetFields())
				{
					if (!field.IsInstanceField || field.DeclaringType != type)
						continue;

					serializeBuffer.AppendF($"\n\tctx.Serialize(\"{field.Name}\", {field.Name});");
				}

				Compiler.EmitTypeBody(type, scope $"{SERIALIZE_NAME}{{{serializeBuffer}\n}}\n");
				Compiler.EmitAddInterface(type, typeof(ISerializable));
			}
		}

		[Serializable]
		struct Foo : this(float x, float y)
		{
		}

		public class ComponentHandler<T>
			where T : struct
		{
			uint8* data;
			protected override void GCMarkMembers()
			{
				T* ptr = (T*)data;
				GC.Mark!((*ptr));
			}
		}

		const String cTest0 = Compiler.ReadText("Test0.txt");
		const String cTest1 = new String('A', 12);
		const uint8[?] cTest0Binary = Compiler.ReadBinary("Test0.txt");
		
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

			Foo bar = .(10, 2);
			ISerializable iSer = bar;
			SerializationContext serCtx = scope .();
			iSer.Serialize(serCtx);
			Test.Assert(serCtx.mStr == "x 10\ny 2\n");

			Test.Assert(cTest0 == "Test\n0");
			Test.Assert(cTest1 == "AAAAAAAAAAAA");
			Test.Assert((Object)cTest1 == (Object)"AAAAAAAAAAAA");
			Test.Assert((cTest0Binary[0] == (.)'T') && ((cTest0Binary.Count == 6) || (cTest0Binary.Count == 7)));
		}
	}
}
