using System;
using System.Diagnostics;
using System.Reflection;
using System.Collections;

namespace Tests
{
	class Comptime
	{
		[AttributeUsage(.All)]
		struct AddFieldAttribute : Attribute
		{
			public Type mType;
			public String mName;
			public int mVal;

			public this(Type type, String name, int val)
			{
				mType = type;
				mName = name;
				mVal = val;
			}
		}

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
			public void ApplyToMethod(MethodInfo method)
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

		[AddField(typeof(float), "D", 4)]
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
				if (var addFieldAttr = typeof(Self).GetCustomAttribute<AddFieldAttribute>())
					Compiler.EmitTypeBody(typeof(Self), scope $"public {addFieldAttr.mType} {addFieldAttr.mName} = {addFieldAttr.mVal};");
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

#unwarn
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

				Runtime.Assert(!type.IsUnion);

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

		[CheckEnum]
		enum EnumA
		{
			case A(int64 aa);
			case B(float bb);
		}

		[AttributeUsage(.All)]
		public struct CheckEnumAttribute : Attribute, IComptimeTypeApply
		{
			public void ApplyToType(Type type)
			{
				int fieldIdx = 0;
				for (var field in type.GetFields())
				{
					switch (fieldIdx)
					{
					case 0:
						Test.Assert(field.Name == "$payload");
						Test.Assert(field.MemberOffset == 0);
						Test.Assert(field.FieldType == typeof(int64));
					case 1:
						Test.Assert(field.Name == "$discriminator");
						Test.Assert(field.MemberOffset == 8);
						Test.Assert(field.FieldType == typeof(int8));
					}
					fieldIdx++;
				}
				Test.Assert(fieldIdx == 4);
			}
		}

		const String cTest0 = Compiler.ReadText("Test0.txt");
		const String cTest1 = new String('A', 12);
		const uint8[?] cTest0Binary = Compiler.ReadBinary("Test0.txt");

		class ClassB<T> where T : const int
		{
			public typealias TA = comptype(GetVal(10, T));
			public const int cTimesTen = 10 * T;
			
			[Comptime]
			static Type GetVal(int a, int b)
			{
				return typeof(float);
			}
		}

		struct Yes;
		struct No;

		struct IsDictionary<T>
		{
			public typealias Result = comptype(_isDict(typeof(T)));

			[Comptime]
			private static Type _isDict(Type type)
			{
				if (let refType = type as SpecializedGenericType && refType.UnspecializedType == typeof(Dictionary<,>))
					return typeof(Yes);
				return typeof(No);
			}
		}

		struct GetArg<T, C>
			where C : const int
		{
			public typealias Result = comptype(_getArg(typeof(T), C));

			[Comptime]
			private static Type _getArg(Type type, int argIdx)
			{
				if (let refType = type as SpecializedGenericType)
					return refType.GetGenericArg(argIdx);
				return typeof(void);
			}
		}

		public class DictWrapper<T> where T : var
		{
			private T mValue = new .() ~ delete _;
		}

		extension DictWrapper<T>
			where T : var
			where IsDictionary<T>.Result : Yes
		{
			typealias TKey = GetArg<T, const 0>.Result;
			typealias TValue = GetArg<T, const 1>.Result;
			typealias KeyValuePair = (TKey key, TValue value);
			typealias KeyRefValuePair = (TKey key, TValue* valueRef);

			public ValueEnumerator GetValueEnumerator()
			{
				return ValueEnumerator(this);
			}

			public struct ValueEnumerator : IRefEnumerator<TValue*>, IEnumerator<TValue>, IResettable
			{
				private SelfOuter mParent;
				private int_cosize mIndex;
				private TValue mCurrent;

				const int_cosize cDictEntry = 1;
				const int_cosize cKeyValuePair = 2;

				public this(SelfOuter parent)
				{
					mParent = parent;
					mIndex = 0;
					mCurrent = default;
				}

				public bool MoveNext() mut
				{
			        // Use unsigned comparison since we set index to dictionary.count+1 when the enumeration ends.
			        // dictionary.count+1 could be negative if dictionary.count is Int32.MaxValue
					while ((uint)mIndex < (uint)mParent.[Friend]mValue.[Friend]mCount)
					{
						if (mParent.[Friend]mValue.[Friend]mEntries[mIndex].mHashCode >= 0)
						{
							mCurrent = mParent.[Friend]mValue.[Friend]mEntries[mIndex].mValue;
							mIndex++;
							return true;
						}
						mIndex++;
					}

					mIndex = mParent.[Friend]mValue.[Friend]mCount + 1;
					mCurrent = default;
					return false;
				}

				public TValue Current
				{
#unwarn
					get { return mCurrent; }
				}

				public ref TValue CurrentRef
				{
					get mut { return ref mCurrent; } 
				}

				public ref TKey Key
				{
					get
					{
						return ref mParent.[Friend]mValue.[Friend]mEntries[mIndex].mKey;
					}
				}

				public void Reset() mut
				{
					mIndex = 0;
					mCurrent = default;
				}

				public Result<TValue> GetNext() mut
				{
					if (!MoveNext())
						return .Err;
					return Current;
				}

				public Result<TValue*> GetNextRef() mut
				{
					if (!MoveNext())
						return .Err;
#unwarn
					return &CurrentRef;
				}
			}
		}

		public struct GetTupleField<TTuple, C> where C : const int
		{
			public typealias Type = comptype(GetTupleFieldType(typeof(TTuple), C));

			[Comptime]
			private static Type GetTupleFieldType(Type type, int index)
			{
				if (type.IsGenericParam)
				{
					Runtime.Assert(type.IsGenericParam);
					String tName = type.GetFullName(.. scope .());
					Runtime.Assert(tName == "TTuple");
					return typeof(var);
				}
				Runtime.Assert(type.IsTuple);
				return type.GetField(index).Get().FieldType;
			}
		}

		[Comptime]
		static int GetMixinVal()
		{
			int a = 23;
			Compiler.Mixin("a += 100;");
			Compiler.MixinRoot(scope String("b += 200;"));
			return a;
		}

		static Type GetMathRetType<T, T2>()
		{
		    return typeof(T);
		}

		static String GetMathString<T, T2>(String expr)
		{
			if ((typeof(T) == typeof(float)) && (typeof(T2) == typeof(int)))
				return scope $"return a{expr}(T)b;";
			else
				return "return default;";
		}

		static comptype(GetMathRetType<T, T2>()) ComputeMath<T, T2, TExpr>(T a, T2 b, TExpr expr) where TExpr : const String
		{
			Compiler.Mixin(GetMathString<T, T2>(expr));
		}

		class GenClass<TDesc> where TDesc : const String
		{
			[OnCompile(.TypeInit), Comptime]
			static void Init()
			{
				Compiler.EmitTypeBody(typeof(Self), TDesc);
			}
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
			Test.Assert(ca.D == 4);

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

			Test.Assert(cTest0 == "Test\n0" || cTest0 == "Test\r\n0");
			Test.Assert(cTest1 == "AAAAAAAAAAAA");
			Test.Assert((Object)cTest1 == (Object)"AAAAAAAAAAAA");
			Test.Assert((cTest0Binary[0] == (.)'T') && ((cTest0Binary.Count == 6) || (cTest0Binary.Count == 7)));

			ClassB<const 3>.TA f = default;
			Test.Assert(typeof(decltype(f)) == typeof(float));
			Test.Assert(ClassB<const 3>.cTimesTen == 30);

			DictWrapper<Dictionary<int, float>> dictWrap = scope .();
			dictWrap.[Friend]mValue.Add(1, 2.3f);
			dictWrap.[Friend]mValue.Add(2, 3.4f);
			int idx = 0;
			for (var value in dictWrap.GetValueEnumerator())
			{
				if (idx == 0)
					Test.Assert(value == 2.3f);
				else
					Test.Assert(value == 3.4f);
				++idx;
			} 
			Test.Assert(idx == 2);

			var tuple = ((int16)1, 2.3f);
			GetTupleField<decltype(tuple), const 0>.Type tupType0;
			GetTupleField<decltype(tuple), const 1>.Type tupType1;
			Test.Assert(typeof(decltype(tupType0)) == typeof(int16));
			Test.Assert(typeof(decltype(tupType1)) == typeof(float));

			int b = 34;
			Test.Assert(GetMixinVal() == 123);
			Test.Assert(b == 234);

			float math = ComputeMath(2.3f, 2, "*");
			Test.Assert(math == 4.6f);
			float math2 = ComputeMath<float, int, "+">(2.3f, 1, "+");
			Test.Assert(math2 == 3.3f);

			GenClass<
				"""
				public int mA = 123;
				"""> genClass = scope .();
			Test.Assert(genClass.mA == 123);
		}
	}
}
