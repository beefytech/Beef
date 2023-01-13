#pragma warning disable 168
using System;
using System.Diagnostics;
using System.Reflection;

namespace Tests
{
	class Interfaces
	{
		interface IFaceA
		{
			int FuncA(int a) mut;
			int FuncA2() mut { return 0; }
		}

		interface IFaceB : IFaceA
		{
			void FuncB(int a) mut
			{
				FuncA(a + 100);
			}

			void FuncC(int a) mut
			{
				FuncB(a + 1000);
			}
		}

		interface IFaceC
		{
			concrete IFaceA GetConcreteIA();
		}

		struct StructA : IFaceB
		{
			public int mA = 10;

			int IFaceA.FuncA(int a) mut
			{
				mA += a;
				return mA;
			}
		}

		struct StructB : IFaceC
		{
			public StructA GetConcreteIA()
			{
				return StructA();
			}
		}

		static int UseIA<T>(mut T val, int a) where T : IFaceA
		{
			return val.FuncA(a);
		}

		static int UseIA2<T>(mut T val) where T : IFaceA
		{
			return val.FuncA2();
		}

		static void UseIB<T>(mut T val, int a) where T : IFaceB
		{
			val.FuncB(a);
		}

		class ClassA : IFaceA
		{
			public int FuncA(int a)
			{
				return 5;
			}

			public virtual int FuncA2()
			{
				return 50;
			}
		}

		class ClassB : ClassA
		{
			public new int FuncA(int a)
			{
				return 6;
			}

			public override int FuncA2()
			{
				return 60;
			}
		}

		class ClassC : ClassA, IFaceA
		{
			public new int FuncA(int a)
			{
				return 7;
			}

			public override int FuncA2()
			{
				return 70;
			}
		}

		public struct Rect
		{
			public float x, y, width, height;
		}

		public class Component
		{

		}

		public class Hitbox : Component
		{
		    public Rect mArea = .() { x=1, y=2, width=3, height=4, };
		}

		public interface IEntity
		{
		    public T GetComponent<T>() where T : Component;
		}

		public interface IHitbox : IEntity
		{
			public Hitbox HitboxValue { get; set; }

		    public Hitbox Hitbox
			{
		        get
				{
		            if (HitboxValue == null)
					{
		                HitboxValue = GetComponent<Hitbox>();
		            }

		            return HitboxValue;
		        }
		    }
		}

		class GameItem : IEntity, IHitbox
		{
			public T GetComponent<T>() where T : Component
			{
				return new T();
			}
			public Hitbox HitboxValue { get; set; }
		}

		static Hitbox GetHitbox<T>(T val) where T : IHitbox
		{
			return val.Hitbox;
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa = .();
			UseIB(mut sa, 9);
			Test.Assert(sa.mA == 119);

			ClassA ca = scope ClassA();
			ClassB cb = scope ClassB();
			ClassA cba = cb;
			ClassC cc = scope ClassC();
			ClassA cca = cc;

			Test.Assert(UseIA(ca, 100) == 5);
			Test.Assert(UseIA(cb, 100) == 5);
			Test.Assert(UseIA(cba, 100) == 5);
			Test.Assert(UseIA((IFaceA)cba, 100) == 5);
			Test.Assert(UseIA(cc, 100) == 7);
			Test.Assert(UseIA(cca, 100) == 5);

			Test.Assert(UseIA2(ca) == 50);
			Test.Assert(UseIA2(cb) == 60);
			Test.Assert(UseIA2(cba) == 60);
			Test.Assert(UseIA2((IFaceA)cba) == 60);
			Test.Assert(UseIA2(cc) == 70);
			Test.Assert(UseIA2(cca) == 70);

			IFaceA ifa = cba;
			Test.Assert(ifa.GetType() == typeof(ClassB));

			ClassF cf = scope .();
			TestIFaceD(cf);
			Test.Assert(cf.mA == 999);
			ClassG cg = scope .();
			TestIFaceD(cg);
			Test.Assert(cg.mA == 999);

			GameItem gameItem = scope .();
			var hitbox = GetHitbox(gameItem);
			Test.Assert(hitbox.mArea == .() { x=1, y=2, width=3, height=4 });
			delete hitbox;
		}

		////

		interface IFaceD<T> 
		{
			T GetVal();

			T Add<T2>(T lhs, T2 rhs) where T : operator T + T2
			{
				return lhs + rhs;
			}

			static T SMethod(T val);

			static T SMethod2(T val)
			{
				return val;
			}
		}

		extension IFaceD<T>
		{
			T GetVal2()
			{
				return GetVal();
			}
		}

		class ClassD : IFaceD<int16>
		{
			public int16 GetVal()
			{
				return 123;
			}

			public int16 Add<T2>(int16 lhs, T2 rhs) where int16 : operator int16 + T2
			{
				return lhs + rhs + 1000;
			}

			public static int16 SMethod(int16 val)
			{
				return val + 2000;
			}
		}

		class ClassE : IFaceD<int16>
		{
			public int16 GetVal()
			{
				return 234;
			}

			public static int16 SMethod(int16 val)
			{
				return val + 3000;
			}

			public static int16 SMethod2(int16 val)
			{
				return val + 4000;
			}
		}

		public static int IDAdd<T>(T val) where T : IFaceD<int16>
		{
			return val.Add((int16)23, (int8)100);
		}

		public static T2 SGet<T, T2>(T val) where T : IFaceD<T2>
		{
			return T.SMethod(val.GetVal());
		}

		public static T2 SGet2<T, T2>(T val) where T : IFaceD<T2>
		{
			return T.SMethod2(val.GetVal());
		}

		interface IFaceD
		{
			int32 Val
			{
				get; set;
			}
		}

		class ClassF : IFaceD
		{
			public int32 mA;

			int32 IFaceD.Val
			{
				get
				{
					return mA;
				}

				set
				{
					mA = value;
				}
			}
		}

		class ClassG : ClassF
		{

		}

		static void TestIFaceD<T>(T val) where T : IFaceD
		{
			val.Val = 999;
		}

		static int TestIFaceD2<T>(T val) where T : IFaceD
		{
			return val.Val;
		}

		interface IParsable
		{
			public enum Error
			{
				case Unknown;
				case Syntax(int pos);
				case InvalidValue;
				case UnexpectedEnd;
			}

			static Self GetDefault(Self[] arr);
			static Result<Self> Parse(StringView sv, Self defaultVal);
			static Result<Self, Error> ParseEx(StringView str)
			{
				Self[] arr = null;
				Self defVal = GetDefault(arr);
				switch (Parse(str, default(Self)))
				{
				case .Ok(let val): return .Ok(val);
				case .Err: return .Err(.Unknown);
				}
			}
		}

		class ClassH : IParsable
		{
			static Self IParsable.GetDefault(ClassH[] arr)
			{
				return default;
			}

			public static Result<Self> Parse(StringView sv, ClassH defaultVal)
			{
				return .Err;
			}
		}


		[AttributeUsage(.Method)]
		struct MethodTestAttribute : Attribute, IComptimeMethodApply
		{
			public static String gLog = new .() ~ delete _;

			[Comptime] 
			public void ApplyToMethod(MethodInfo method)
			{
				Compiler.EmitMethodEntry(method, "int b = 2;");
			}
		}

		struct Test4<T>
		{
			[Comptime, OnCompile(.TypeInit)]
			public static void Generator()
			{
				T val = default;
				Compiler.EmitTypeBody(typeof(Self), "int test = 0;");
			}

			[MethodTest]
			public void Zank<T2>()
			{
				int c = b;
			}

			public void Test() mut
			{
				test = 1;
			}
		}

		class Test5
		{
			public Result<T> TryGetValue<T>() where T : IParseable
		    {
				T val = default;
				var s = T.Serialize(val);
				return T.Deserialize(s);
		    }
		}

		interface IParseable
		{
		    public static Result<Self> Deserialize(StringView value);
		    public static String Serialize(Self value);
		}

		public class EngineTag : LibA.ITaggable
		{
			public String Tag
			{
				get => "ET";
			}
		}

		[Test]
		public static void TestDefaults()
		{
			ClassD cd = scope .();
			IFaceD<int16> ifd = cd;

			int v = ifd.GetVal();

			Test.Assert(v == 123);
			v = ifd.GetVal2();
			Test.Assert(v == 123);
			v = IDAdd(cd);
			Test.Assert(v == 1123);
			v = SGet<ClassD, int16>(cd);
			Test.Assert(v == 2123);
			v = SGet2<ClassD, int16>(cd);
			Test.Assert(v == 123);

			ClassE ce = scope .();
			ifd = ce;
			v = ifd.GetVal();
			Test.Assert(v == 234);
			v = ifd.GetVal2();
			Test.Assert(v == 234);
			v = IDAdd(ce);
			Test.Assert(v == 123);
			v = SGet<ClassE, int16>(ce);
			Test.Assert(v == 3234);
			v = SGet2<ClassE, int16>(ce);
			Test.Assert(v == 4234);

			ClassF cf = scope .();
			TestIFaceD(cf);
			Test.Assert(cf.mA == 999);
			Test.Assert(TestIFaceD2(cf) == 999);
			ClassG cg = scope .();
			TestIFaceD(cg);
			Test.Assert(cg.mA == 999);
			Test.Assert(TestIFaceD2(cg) == 999);

			ClassH ch = scope .();
		}
	}
}
